# Eden SX2A NCE EXP04

Continuação do EXP03 R2. Android 13+/API 33, ARM64, pacote dev.eden.nx2,
versionCode 34192005 e versão nx2-nce-exp04-sx2a-0.3.0. A assinatura é a mesma
chave de desenvolvimento do EXP02/EXP03 R2 e da base pública 0.3.0.

## Mudanças desta versão

- Cache Vulkan em arquivo separado por fabricante, dispositivo e pipelineCacheUUID.
  Ao voltar a um driver compatível já usado, seu cache anterior continua disponível.
  O arquivo antigo vulkan_pipelines.bin pode ser importado quando o cabeçalho é compatível.
- Validação do cabeçalho Vulkan de 32 bytes, versão, fabricante, dispositivo e UUID,
  antes de entregar o cache ao driver. Arquivos truncados, cabeçalhos inválidos e
  arquivos maiores que 256 MiB são ignorados antes de alocar seu conteúdo.
- Até três tentativas de obter uma cópia completa do cache quando ele cresce durante
  a consulta. Falhas de consulta, leitura, escrita ou substituição preservam o arquivo anterior.
  Ao fechar, as filas de compilação/gravação terminam antes da gravação final. Isso pode
  prolongar a saída quando ainda há shaders sendo compilados.
- Metadados NACP: comprimento comprimido limitado à capacidade do campo; descompressão
  deve terminar e produzir registros completos. Evita leitura fora do campo e cópia
  além do vetor quando o resultado contém um registro parcial. Metadados curtos são rejeitados.
  Mantém o suporte existente a até 32 idiomas, incluindo português brasileiro.
- Erros específicos para main ausente e main.npdm inválido, aproveitando o diálogo anterior.
  O botão Copiar detalhes copia a causa sem fechar o diálogo.

## Validação

187 verificações novas com ASan/UBSan e o zlib 1.3.2 do projeto: cabeçalhos de driver,
UUID, crescimento/redução do cache, falhas, preservação do arquivo anterior, arquivos
reais/sparse, limites e tabelas comprimidas válidas/truncadas/parciais/excessivas.
As 42 verificações de carregamento/memória/quadros do EXP03 e oito verificações ZBIC
também foram executadas nesta etapa. O relatório do APK contém o resultado efetivo
da compilação e da verificação de assinatura, ABI, JNI, DEX e alinhamento.

O NCE EXP02 foi mantido sem novas instruções. Não há teste em telefone ou ganho de FPS
medido nesta etapa. As mudanças de cache visam evitar recompilações desnecessárias e
falhas de inicialização; elas não comprovam melhora no tempo de quadro. Também não
adicionam task/mesh shaders ou suporte gráfico completo de Switch 2. Arquivos de jogo
incompletos continuam precisando de uma cópia íntegra. A compatibilidade entre as
versões de ExeFS e RomFS não é verificada automaticamente.

## Reproduzir

NCE_EXP03R2.md e NCE_EXP02.md documentam a base e as ferramentas. Para os novos testes:

```sh
python3 .ci/android/test-exp04.py --zlib-source CAMINHO_ZLIB --vulkan-headers CAMINHO_VULKAN_HEADERS
```

Use o mesmo build CMake Release/API 33 e gere o DEX atualizado com
.ci/android/build-loader-error-dex.py. Reempacote com .ci/android/repack-nce.py,
alinhe com zipalign -P 16 -f 4 e assine com a chave de desenvolvimento existente.
O frontend DEX original da base pública permanece intacto; o diálogo tem DEX próprio.
Textos antigos de versão/recomendação Dynarmic do frontend podem permanecer visíveis;
o manifesto e o núcleo informam EXP04.

Referências usadas para o tratamento dos retornos e formatos:
- https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html
- https://zlib.net/manual.html

GPL-3.0-or-later e licenças originais preservadas. Alterações preparadas com assistência
de IA (Codex), sem envio de commits ou PRs ao repositório. Sem validação em hardware por nós.
