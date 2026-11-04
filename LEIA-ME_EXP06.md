# Eden NX2 EXP06 — fontes experimentais para Android ARM64

Base: fontes da EXP05 compilada anteriormente, derivada da EXP04 fornecida pelo usuário.
Alvo: Motorola Edge 20 Pro, Snapdragon 870, Adreno 650, Android 13/API 33.
Data: 04/11/2026. Alterações assistidas por ChatGPT; revisão humana necessária antes de contribuição upstream. Licenças originais preservadas.

## Alterações desta entrega

- NCE: validação do intervalo de código antes de modificar o estado do patcher; checagem por subtração evita overflow. Leitura de instruções com memcpy elimina dependência de alinhamento e aliasing no scanner. Não altera semântica das instruções PAC/MTE existentes.
- NSO: substitui repetição ilimitada de PatchText por uma única nova tentativa com patcher separado. Se também falhar, retorna erro de carregamento em vez de continuar criando patchers.
- NRO: não prossegue para relocação quando PatchText retorna falha.
- Vulkan/Android: orçamento inicial da GPU integrada usa subtração saturada de uso, limite da capacidade local e reserva de 1/8 (máximo 1 GiB), com teto de 4/6 GiB conforme modo. Impede que capacidade física seja usada como piso acima do orçamento livre. Caminho desktop preservado. É uma política experimental: pode aumentar descarte/recompilação de cache em aparelhos sob pressão; comparar frametime e uso de memória.
- Shaders: reutiliza os pools por thread também na reconstrução de pipelines gráficos do cache em disco, liberando blocos extras ao terminar cada tarefa. Mantém a reutilização de runtime da EXP05. Não muda a ordem de desenhos nem oculta shaders que falham.
- Build: timeout de 120 s e de inatividade de 30 s no download do checksum CPM, mantendo verificação TLS e hashes. Não remove o acesso à rede nem corrige indisponibilidade do servidor.
- Workflow manual para APK ARM64, relatório de verificação, símbolos e logs. Dois jobs de compilação por padrão para reduzir pressão de RAM.

## Como usar no GitHub Actions

1. Extraia este ZIP e coloque TODO seu conteúdo na raiz do repositório que você escolher, incluindo `.github`, `.ci`, `CMakeLists.txt`, `src`, `externals` e `ci-inputs`. Não coloque o ZIP inteiro nem uma pasta extra no lugar do projeto.
2. Faça o commit por sua conta. Este trabalho não criou repositório, commit, PR nem push.
3. Abra Actions → Eden EXP06 Android ARM64 → Run workflow.
4. Depois de passar a compilação e verificação, baixe o artifact `Eden-EXP06-ARM64`. Os logs são enviados mesmo se a compilação falhar.

O workflow baixa dependências públicas descritas pelo próprio projeto. Precisa de acesso a esses servidores e de um runner Ubuntu 24.04 com SDK Android (padrão do GitHub hospedado). Usa NDK 28.2.13676358, CMake 3.31.6, Ninja 1.13.0, JDK 17 e Build Tools 35.0.0. Não depende de repositório pessoal externo. Referências: https://github.com/android/ndk/releases e https://docs.github.com/en/actions/tutorials/store-and-share-data .

Incluído `ci-inputs/Eden_EXP04.apk` como base da interface. O workflow recompila a biblioteca nativa, reaproveita DEX/recursos/drivers da EXP04, atualiza versão para EXP06 (34192007), alinha e assina com a chave de teste já presente no projeto. NÃO é uma reconstrução Gradle da interface Java/Kotlin. A base é verificada por SHA256; não substitua esse APK por outro sem adaptar os verificadores. Não há chaves de jogos, firmware ou ROMs neste pacote.

A assinatura é de desenvolvimento, compatível com a EXP04 fornecida. Não é uma chave privada de produção. Caso uma instalação existente tenha outro certificado, a atualização poderá ser recusada; preserve seus dados antes de qualquer reinstalação.

## Validação realmente executada

- Testes C++ de leitura de código: limites, overflow de intervalo e 100.000 leituras em armazenamento desalinhado.
- Política de memória: casos de orçamento exaurido/zero, tetos e 1.000.000 triplas aleatórias em dois modos (2.000.000 avaliações).
- Filtro NCE herdado: 2.204.288 comparações com referência EXP04.
- ObjectPool: reuso, limite exato, crescimento, destrutores, limpeza sem alocação e duas threads.
- Acima executados em host x86_64 com ASan/UBSan; LeakSanitizer desativado no ambiente.
- Scripts Python compilados; Bash validado com bash -n; YAML parseado; patch diferencial e integridade ZIP verificados.
- Testes locais não compilam as unidades completas patcher.cpp, nso.cpp, nro.cpp ou vulkan_device.cpp. A integração Android e os testes NCE com assembler LLVM são passos do Actions ainda NÃO executados nesta entrega.

## Limites e continuidade no outro chat

Não há APK EXP06 previamente compilado neste ZIP. O APK em ci-inputs é EXP04, apenas entrada de empacotamento. Não há medição no celular nem confirmação de ganho de FPS, correção do céu preto ou dos crashes de Pokopia. Não adicionamos novos serviços Switch 2 nem novas instruções suportadas. Vulkan 1.4 não é anunciado artificialmente.

O orçamento da GPU é uma estimativa inicial, não a memória livre total do Android. Um orçamento zero continua seguindo o tratamento existente de ausência de orçamento nos consumidores; não há uma política nova de recuperação de OOM. O céu preto precisa de reprodução vinculada ao driver efetivamente carregado, extensões e shaders envolvidos.

Para o outro chat: use este pacote como base, leia EXP06_changes.patch e VALIDATION/host-tests.log, execute o workflow primeiro e corrija eventuais erros reais de integração. Não declare suporte Switch 2 ou desempenho por compilar. Compare EXP05 e EXP06 no mesmo driver, configuração, cena e temperatura, com cache frio e quente separados. Colete backtrace simbolizado usando a biblioteca não removida de símbolos do artifact. Priorize quaisquer regressões antes de novas otimizações.
