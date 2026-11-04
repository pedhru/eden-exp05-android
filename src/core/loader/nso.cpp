// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <openssl/evp.h>
#include <span>
#include <vector>

#include "common/common_funcs.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/lz4_compression.h"
#include "common/zbic_compression.h"
#include "common/settings.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/loader/nso.h"
#include "core/loader/nso_segment_reader.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {
namespace {
struct MODHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(MODHeader) == 0x1c, "MODHeader has incorrect size.");

constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}
} // Anonymous namespace

bool NSOHeader::IsSegmentCompressed(size_t segment_num) const {
    ASSERT_MSG(segment_num < 3, "Invalid segment {}", segment_num);
    return ((flags >> segment_num) & 1) != 0;
}

AppLoader_NSO::AppLoader_NSO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {}

FileType AppLoader_NSO::IdentifyType(const FileSys::VirtualFile& in_file) {
    u32 magic = 0;
    if (in_file->ReadObject(&magic) != sizeof(magic)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') != magic) {
        return FileType::Error;
    }

    return FileType::NSO;
}

std::optional<VAddr> AppLoader_NSO::LoadModule(Kernel::KProcess& process, Core::System& system, const FileSys::VfsFile& nso_file, VAddr load_base, bool should_pass_arguments, bool load_into_process, std::optional<FileSys::PatchManager> pm, std::vector<Core::NCE::Patcher>* patches, s32 patch_index) {
    const auto fail = [&](std::string message) -> std::optional<VAddr> {
        LOG_ERROR(Loader, "{}", message);
        SetLastLoadError(std::move(message));
        return std::nullopt;
    };
    constexpr std::array SegmentNames{"TEXT", "RODATA", "DATA"};
    if (nso_file.GetSize() < sizeof(NSOHeader)) {
        return fail(fmt::format("{}: incomplete NSO header ({} bytes).", nso_file.GetName(),
                                nso_file.GetSize()));
    }
    NSOHeader nso_header{};
    if (sizeof(NSOHeader) != nso_file.ReadObject(&nso_header)) {
        return fail(fmt::format("{}: could not read the complete NSO header.", nso_file.GetName()));
    }
    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return fail(fmt::format("{}: invalid NSO signature.", nso_file.GetName()));
    }
    if (nso_header.version != 0) {
        return fail(fmt::format("{}: unsupported NSO header version {}.", nso_file.GetName(),
                                static_cast<u32>(nso_header.version)));
    }

    // Allocate some space at the beginning if we are patching in PreText mode.
    const size_t module_start = [&]() -> size_t {
#ifdef HAS_NCE
        if (patches && load_into_process) {
            auto* patch = &patches->operator[](patch_index);
            if (patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
                return patch->GetSectionSize();
            } else if (patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
                return patch->GetPreSectionSize();
            }
        }
#endif
        return 0;
    }();

    u64 data_end{};
    for (size_t i = 0; i < nso_header.segments.size(); ++i) {
        const auto& segment = nso_header.segments[i];
        const bool compressed = nso_header.IsSegmentCompressed(i);
        const auto error = Detail::ValidateNsoSegmentRange(
            nso_file.GetSize(), segment.offset, segment.size,
            nso_header.segments_compressed_size[i], compressed);
        if (error != Detail::NsoReadError::None) {
            const u64 stored_size = compressed ? static_cast<u32>(nso_header.segments_compressed_size[i])
                                               : static_cast<u32>(segment.size);
            const u64 end = u64{segment.offset} + stored_size;
            SetLastLoadError(error == Detail::NsoReadError::OutOfBounds
                ? fmt::format("{} / {}: file has {} bytes; segment ends at {} ({} bytes missing).",
                              nso_file.GetName(), SegmentNames[i], nso_file.GetSize(), end,
                              end - nso_file.GetSize())
                : fmt::format("{} / {}: invalid compressed or decoded segment size.",
                              nso_file.GetName(), SegmentNames[i]));
            LOG_ERROR(Loader, "Invalid NSO {} segment {}: error={} offset={} stored={} file_size={}",
                      nso_file.GetName(), i, static_cast<int>(error),
                      static_cast<u32>(segment.offset), stored_size, nso_file.GetSize());
            return std::nullopt;
        }
        data_end = std::max(data_end, u64{segment.location} + segment.size);
    }
    const u64 argument_size = should_pass_arguments && !Settings::values.program_args.GetValue().empty()
                                 ? NSO_ARGUMENT_DATA_ALLOCATION_SIZE : 0;
    constexpr u64 MaxImageSize = std::numeric_limits<int>::max() - Core::Memory::YUZU_PAGEMASK;
    const u64 extra_size = u64{nso_header.segments[2].bss_size} + argument_size;
    if (module_start > MaxImageSize || data_end > MaxImageSize - module_start ||
        extra_size > MaxImageSize - module_start - data_end) {
        return fail(fmt::format("{}: image size exceeds the loader's address range.", nso_file.GetName()));
    }
    Kernel::CodeSet codeset;
    codeset.memory.reserve(PageAlignSize(static_cast<u32>(module_start + data_end + extra_size)));
    codeset.memory.resize(module_start + data_end);
    {
        std::vector<u8> compressed_data;
        for (size_t i = 0; i < nso_header.segments.size(); ++i) {
            const auto& segment = nso_header.segments[i];
            const auto output = std::span<u8>{codeset.memory.data(), codeset.memory.size()}.subspan(
                module_start + segment.location, segment.size);
            const auto result = Detail::ReadNsoSegment(
                nso_file.GetSize(), segment.offset, nso_header.segments_compressed_size[i], output,
                nso_header.IsSegmentCompressed(i), compressed_data,
                [&](u8* destination, size_t size, u64 offset) {
                    return nso_file.Read(destination, size, offset);
                },
                [&](std::span<u8> destination, std::span<const u8> input) {
                    return nso_header.IsZBICCompressed()
                        ? Common::Compression::DecompressDataZBIC(destination, input)
                        : Common::Compression::DecompressDataLZ4(destination.data(), destination.size(),
                                                                 input.data(), input.size());
                });
            if (result.error != Detail::NsoReadError::None) {
                return fail(fmt::format(
                    "{} / {}: read or decompression failed (read {}, expected {}, decoded {}).",
                    nso_file.GetName(), SegmentNames[i], result.bytes_read, output.size(),
                    result.decoded_size));
            }
            if ((nso_header.flags & (1U << (i + 3))) != 0) {
                NSOHeader::SHA256Hash hash{};
                unsigned int hash_size{};
                if (EVP_Digest(output.data(), output.size(), hash.data(), &hash_size, EVP_sha256(), nullptr) != 1 ||
                    hash_size != hash.size() || hash != nso_header.segment_hashes[i]) {
                    return fail(fmt::format("{} / {}: SHA-256 does not match the NSO header.",
                                            nso_file.GetName(), SegmentNames[i]));
                }
            }
            codeset.segments[i].addr = module_start + segment.location;
            codeset.segments[i].offset = module_start + segment.location;
            codeset.segments[i].size = segment.size;
        }
    }

    if (should_pass_arguments && !Settings::values.program_args.GetValue().empty()) {
        const auto arg_data{Settings::values.program_args.GetValue()};
        if (arg_data.size() > NSO_ARGUMENT_DATA_ALLOCATION_SIZE - sizeof(NSOArgumentHeader)) {
            return fail(fmt::format("{}: program arguments exceed their allocation.", nso_file.GetName()));
        }

        codeset.DataSegment().size += NSO_ARGUMENT_DATA_ALLOCATION_SIZE;
        NSOArgumentHeader args_header{NSO_ARGUMENT_DATA_ALLOCATION_SIZE, static_cast<u32_le>(arg_data.size()), {}};
        const auto end_offset = codeset.memory.size();
        codeset.memory.resize(u32(codeset.memory.size()) + NSO_ARGUMENT_DATA_ALLOCATION_SIZE);
        std::memcpy(codeset.memory.data() + end_offset, &args_header, sizeof(NSOArgumentHeader));
        std::memcpy(codeset.memory.data() + end_offset + sizeof(NSOArgumentHeader), arg_data.data(), arg_data.size());
    }

    codeset.DataSegment().size += nso_header.segments[2].bss_size;
    u32 image_size = PageAlignSize(u32(codeset.memory.size()) + nso_header.segments[2].bss_size);
    codeset.memory.resize(image_size);

    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        codeset.segments[i].size = PageAlignSize(codeset.segments[i].size);
    }

    // Apply patches if necessary
    const auto name = nso_file.GetName();
    if (pm && (pm->HasNSOPatch(nso_header.build_id, name) || Settings::values.dump_nso)) {
        std::span<u8> patchable_section(codeset.memory.data() + module_start, codeset.memory.size() - module_start);
        std::vector<u8> pi_header(sizeof(NSOHeader) + patchable_section.size());
        std::memcpy(pi_header.data(), &nso_header, sizeof(NSOHeader));
        std::memcpy(pi_header.data() + sizeof(NSOHeader), patchable_section.data(),
                    patchable_section.size());

        pi_header = pm->PatchNSO(pi_header, name);

        std::copy(pi_header.begin() + sizeof(NSOHeader), pi_header.end(), patchable_section.data());
    }

#ifdef HAS_NCE
    // If we are computing the process code layout and using nce backend, patch.
    const auto& code = codeset.CodeSegment();
    auto* patch = patches ? &patches->operator[](patch_index) : nullptr;
    if (patch && !load_into_process) {
        //Set module ID using build_id from the NSO header
        patch->SetModuleID(nso_header.build_id);
        // Patch SVCs and MRS calls in the guest code
        if (!patch->PatchText(codeset.memory, code)) {
            const bool is_nx2 = patch->IsNx2();
            patch = &patches->emplace_back(is_nx2);
            patch->SetModuleID(nso_header.build_id);  // In case the patcher is changed for big modules, the new patcher should also have the build_id
            if (!patch->PatchText(codeset.memory, code)) {
                SetLastLoadError("NCE: invalid code segment or unreachable patch layout.");
                LOG_ERROR(Loader, "NCE patching failed for NSO {}", nso_file.GetName());
                return std::nullopt;
            }
        }
    } else if (patch) {
        // Relocate code patch and copy to the program image.
        // Save size before RelocateAndCopy (which may resize)
        const size_t size_before_relocate = codeset.memory.size();
        if (patch->RelocateAndCopy(load_base, code, codeset.memory, &process.GetPostHandlers())) {
            // Update patch section.
            auto& patch_segment = codeset.PatchSegment();
            auto& post_patch_segment = codeset.PostPatchSegment();
            const auto patch_mode = patch->GetPatchMode();
            if (patch_mode == Core::NCE::PatchMode::PreText) {
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else if (patch_mode == Core::NCE::PatchMode::Split) {
                // For Split-mode, we are using pre-patch buffer at start, post-patch buffer at end
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetPreSectionSize());
                post_patch_segment.addr = size_before_relocate;
                post_patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else {
                patch_segment.addr = image_size;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            }
        }

        // Refresh image_size to take account the patch section if it was added by RelocateAndCopy
        image_size = static_cast<u32>(codeset.memory.size());
    }
#endif

    // If we aren't actually loading (i.e. just computing the process code layout), we are done
    if (!load_into_process) {
#ifdef HAS_NCE
        // Ok, so for Split mode, we need to account for pre-patch and post-patch space
        // which will be added during RelocateAndCopy in the second pass. Where it crashed
        // in Android Studio at PreText. May be a better way. Works for now.
        if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
            return load_base + patch->GetPreSectionSize() + image_size + patch->GetSectionSize();
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
            return load_base + patch->GetSectionSize() + image_size;
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PostData) {
            return load_base + image_size + patch->GetSectionSize();
        }
#endif
        return load_base + image_size;
    }

    // Apply cheats if they exist and the program has a valid title ID
    if (pm) {
        // TODO(Maufeat): Check if there is a better way to check
        if (name == "main")
            system.SetApplicationProcessBuildID(nso_header.build_id);

        const auto cheats = pm->CreateCheatList(nso_header.build_id);
        if (!cheats.empty()) {
            system.RegisterCheatList(cheats, nso_header.build_id, load_base, image_size);
        }
    }

    // Load codeset for current process
    LOG_INFO(Loader, "Module {} loaded at {:#x} (size {:#x})", nso_file.GetName(), load_base, image_size);
    process.LoadModule(system.Kernel(), std::move(codeset), load_base);
    return load_base + image_size;
}

AppLoader_NSO::LoadResult AppLoader_NSO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    modules.clear();

    // Load module
    const VAddr base_address = GetInteger(process.GetEntryPoint());
    if (!LoadModule(process, system, *file, base_address, true, true)) {
        return {ResultStatus::ErrorLoadingNSO, {}};
    }

    modules.insert_or_assign(base_address, file->GetName());
    LOG_DEBUG(Loader, "loaded module {} @ {:#x}", file->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NSO::ReadNSOModules(Modules& out_modules) {
    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
