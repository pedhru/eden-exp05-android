# EXP05: integração experimental de SparseTiles e TemporalUpscale

Esta entrega modifica de fato o código-fonte EXP04. Não é apenas um diff de referência.

## Código integrado
- SparseTiles: cache de texturas, flags, alocação Vulkan, capacidades do dispositivo, rotinas de bind e engine.
- TemporalUpscale: shader GLSL, CMake de shaders, pipeline de blit, cache de histórico e interface do rasterizer.
- NUP não foi portado.
- SparseTiles fica DESATIVADO por padrão. Habilitar somente em teste controlado com variável de ambiente `EDEN_EXP05_ENABLE_SPARSE=1` no processo.
- TemporalUpscale segue o fluxo de ativação upstream quando a operação do jogo o solicitar.

## Limitações importantes
- Os arquivos relacionados foram transplantados da SX2A 0.3.2 para a EXP04, portanto podem carregar outras alterações upstream além dessas duas funções.
- A substituição de arquivos vulkan_device e allocator pode conflitar com otimizações exclusivas da EXP04. É uma branch experimental, não substituição da EXP04 estável.
- Não foi possível compilar: ambiente sem Android SDK/NDK e dependências de build. Compatibilidade de ABI e execução no Edge 20 Pro NÃO verificadas.
- Testes mínimos: CMake/NDK, testes de renderização Vulkan, regressão NCE, ativação gradual de sparse, verificação de histórico temporal e logs de crash.
- O patch `EXP05_sparse_temporal.patch` permite revisar todas as alterações antes de mesclar.
