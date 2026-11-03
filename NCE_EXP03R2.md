# Eden SX2A NCE EXP03 R2

Recompilação das alterações do EXP03 sobre o fonte EXP02 anexado pelo usuário.
O binário recebe versão própria: nx2-nce-exp03r2-sx2a-0.3.0, versionCode 34192004.
Não é uma recuperação do APK anterior byte a byte. Android 13+/API 33, ARM64,
pacote dev.eden.nx2 e a mesma chave de desenvolvimento da base pública 0.3.0/EXP02.

## Alterações restauradas

- NSO: verificação dos limites antes da alocação; leituras parciais com progresso;
  tamanho exato de descompressão; SHA-256 quando exigido pelo cabeçalho; erros interrompem a carga.
- Descompressão diretamente na imagem final, sem o buffer duplicado de saída.
  Reserva prévia do espaço de BSS/argumentos para evitar realocações intermediárias.
- Segmentos vazios válidos LZ4 e ZBIC são aceitos.
- Caches: orçamentos de texturas abaixo de 2 GiB, subtração segura das reservas e limites do
  cache de buffers proporcionais ao orçamento informado pelo driver.
- Contador de FPS drenado atomicamente e proteção contra divisão por zero/exportação vazia.
  Log automático a cada dez segundos de submissões: FPS e percentis p50/p95/p99/máximo dos
  últimos 512 intervalos. Eles incluem pausas/carregamentos; não medem diretamente o tempo da GPU.
- Diálogo Android para certas falhas NSO/main.npdm com os detalhes do arquivo, em vez de erro
  genérico sobre chaves. Os DEX originais são preservados e um DEX independente é acrescentado.
- NCE EXP02 preservado; o núcleo informa a versão R2 real no log e em getBuildVersion.

## Validação e limites

Testes executados nesta reconstrução: 34.090 verificações NCE com ASan/UBSan e comparação LLVM
(51 substituições diretas e nove instruções emuladas); 42 verificações dos auxiliares com o LZ4
1.10.0 do projeto e um milhão de incrementos concorrentes; oito verificações com o decoder ZBIC
do projeto. LeakSanitizer é desativado no host sem /proc. O diálogo é compilado com javac e D8.
O núcleo Android é compilado com NDK 28.2.13676358, CMake 3.31.5, Release, API 33.
O relatório APK_verification.json registra a verificação efetiva do pacote final.

Não houve teste em telefone, execução de jogo ou medição de ganho de FPS/RAM nesta etapa.
Não foram adicionadas novas instruções NCE, task/mesh shaders, compatibilidade gráfica completa
ou prova automática de que ExeFS/RomFS pertencem à mesma versão. Arquivos do jogo com bytes
ausentes continuam precisando de uma cópia íntegra.

O NCE permanece opcional em Depuração → Backend da CPU. A recomendação Dynarmic e o BuildConfig
antigo da interface pública podem continuar aparecendo; o manifesto e o núcleo identificam R2.

## Reproduzir

Use JDK 17, NDK 28.2.13676358, CMake 3.31.5/Ninja, build-tools 35.0.0, plataforma Android 36 e
glslangValidator no PATH. NCE_EXP02.md documenta a configuração de base. Mantenha
EDEN_NX2_NO_ENV_FILE=ON e CMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF desde a configuração inicial.
Use um TMPDIR gravável. Compile o núcleo, remova símbolos de cópias das bibliotecas com
llvm-strip --strip-unneeded e gere o diálogo:

```sh
python3 .ci/android/build-loader-error-dex.py --jdk "$JAVA_HOME" --sdk "$ANDROID_HOME" --out ui-build
python3 .ci/android/repack-nce.py --base base-0.3.apk --native native-out --loader-error-dex ui-build/dex/classes.dex --out unsigned.apk
zipalign -P 16 -f 4 unsigned.apk aligned.apk
apksigner sign --ks src/android/app/debug.keystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --v4-signing-enabled false --out Eden_EXP03R2.apk aligned.apk
```

O APK-base público deve ter SHA-256 cee27ff587703eb4b881dd7216b4d4267a70653af558cc0fc1efd0ec4fce9717.
O diálogo também integra o fonte normal, com regra ProGuard de preservação para compilação completa.
Licença GPL-3.0-or-later e licenças originais preservadas. Trabalho preparado com assistência de IA
(Codex); não enviado ao repositório. Não há validação em hardware por nós.
