# Eden SX2A NCE EXP02

Build experimental sobre o codigo de `moceure/eden-nx2-android`, tag
`nx2-android-v0.3.0`, commit `d712c0bbf0`.

O NCE usa instrucoes ARM64 nativas e adapta LDAPR, PAC/AUT e operacoes MTE ao
modelo de ponteiros sem assinatura e tags de alocacao zero usado pelo JIT desta
base. PACGA, cargas autenticadas e stores com zeragem/dados sao tratados em
software. Isso nao implementa autenticacao criptografica PAC nem isolamento MTE.
Atomicas LSE continuam nativas; se o Android nao anunciar esse recurso, o loader
usa Dynarmic. NCE tambem exige fastmem e espaco de enderecos de 39 bits.

Inclui adaptacao de NSO e NRO carregados durante a execucao, preservacao de NZCV,
tratamento de registrador zero e correcao do ponto de divisao dos patches para
modulos grandes. Falhas de memoria NX2 deixam de pular instrucoes silenciosamente.

## Usar

Android 13 ou superior, ARM64. O pacote continua `dev.eden.nx2` e e assinado com
a mesma chave de desenvolvimento incluida no codigo da versao publica 0.3.0.
O APK pode atualizar essa versao sem exigir desinstalacao. A instalacao depende
tambem das regras do Android e da assinatura do aplicativo que ja estiver instalado.

Nas configuracoes avancadas, em Depuracao, selecione **Backend da CPU: NCE**.
O checklist de compatibilidade da interface original ainda recomenda Dynarmic;
essa mensagem pertence a base 0.3.0. Confirme o backend efetivo no overlay ou no
log `NX2 CPU backend:`. Para comparar comportamento, selecione Dynarmic no mesmo menu.

Continuam os requisitos de dados, firmware e formato de jogo da base Android.
Nao foram testados jogos em telefone nesta compilacao. Nao ha medicao de FPS ou
garantia de que jogos especificos iniciem.

## Compilar e empacotar

Ferramentas: JDK 17, Android NDK 28.2.13676358, CMake 3.31.5, Ninja,
Android build-tools 35.0.0 e `glslangValidator` disponivel no PATH.
Consulte `.ci/android/build-nce-native.sh` para os parametros do nucleo.

A interface Java e os recursos sao reutilizados do APK da mesma tag, cujo SHA256 e
`cee27ff587703eb4b881dd7216b4d4267a70653af558cc0fc1efd0ec4fce9717`.
O nucleo nativo e recompilado deste codigo. O script de empacotamento exige esse
APK exato, troca as bibliotecas recompiladas e atualiza versao/nome no manifesto
sem alterar o pacote ou os indices originais dos recursos. Alinhe e assine o
resultado com `zipalign -P 16 -f 4` e `apksigner`, usando o debug.keystore da base.
O APK original esta em:

https://git.eden-emu.dev/moceure/eden-nx2-android/releases/download/nx2-android-v0.3.0/eden-sx2a-v0.3.0.apk

```sh
python3 .ci/android/test-nx2-nce.py --clang "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
bash .ci/android/build-nce-native.sh "$ANDROID_NDK" "$CMAKE_HOME" "$BUILD_DIR"
python3 .ci/android/repack-nce.py --base base.apk --native "$BUILD_DIR/out" --out unsigned.apk
```

As alteracoes foram produzidas com assistencia de IA, revisadas e verificadas
automaticamente. Nao foram enviadas ao repositorio do projeto. A validacao em
hardware pelo usuario ainda e necessaria. Licenca GPL-3.0-or-later; veja LICENSE.txt
e os avisos de licenca nos arquivos e dependencias.
