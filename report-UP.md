# PintOS - Relatório de Implementação

## Projeto 2 - User Programs


### Parte 1 - Carregamento de Processos e Passagem de Argumentos
#### `userprog/process.c`, `userprog/process.h`
- Modificados `process_execute()` e `load()` para extrair filename do comando do processo
- Implementado parsing de linha de comando em `setup_stack()` com `strtok_r()`
- Modificado `setup_stack()` para posicionar argumentos na pilha

#### Design
1. Thread pai chama `process_execute(exec_string)`:
    1. Cópia de `exec_string` completa
    2. Extração do `filename` (primeiro token) usando `strcspn()`
    3. Invoca thread `start_process()` de nome `filename`, passando `exec_string` completa como argumento
2. Em `start_process()`, carrega o executável via `load(exec_string)`
3. Em `load()`, extrai novamente `filename` e abre o executável
4. Chama `setup_stack(exec_string, &esp)` que configura a pilha de usuário:
    1. Copia `exec_string` inteira para o topo da pilha
    2. Tokeniza in-place com `strtok_r()` e coleta ponteiros `argv[]`
    3. Alinhamento de stack a múltiplos de 4 bytes
    4. Empilhamento de NULL terminator, ponteiros `argv[]`, `argc` e endereço de retorno falso

#### Resultados de Testes
#### Resultados de Testes
- ✅ `userprog/args-none`: Sem argumentos — testa a execução de um programa sem parâmetros.
- ✅ `userprog/args-single`: Um argumento — valida passagem e leitura de um único argumento.
- ✅ `userprog/args-multiple`: Múltiplos argumentos — verifica parsing e empilhamento de vários argumentos.
- ✅ `userprog/args-many`: Muitos argumentos — testa limites do número de argumentos aceitos pela pilha.
- ✅ `userprog/args-dbl-space`: Espaços duplos tratados corretamente — garante que espaços extras não criem argumentos vazios.

### Parte 2 - Controle de Processos (halt, exit, exec, wait)
#### `userprog/process.c`, `userprog/syscall.c`, `threads/thread.c`
- Implementadas funções de syscall: `halt()`, `exit(status)`, `exec(cmd)`, `wait(pid)`
- Modificada `syscall_handler()` para despachar chamadas de sistema e extrair argumentos da pilha do usuário
- Adicionada impressão do status de saída em `exit()`
- Implementada sincronização entre processos pai e filho usando semáforos e variáveis de condição
- Estruturado gerenciamento de status de saída e comunicação entre threads

#### Design
1. O processo de usuário faz uma chamada de sistema (ex: `exit`, `exec`, `wait`, `halt`)
2. O handler de syscall (`syscall_handler`) identifica o código da syscall e extrai os argumentos da pilha do usuário
3. Para `exec`, uma nova thread de usuário é criada, inicializando estruturas de controle de filho e retornando o tid
4. Para `wait`, o processo pai bloqueia até que o filho termine, usando semáforo/condição para sincronização
5. Para `exit`, o processo registra o status de saída, imprime a mensagem e libera recursos, sinalizando o pai se necessário
6. Em caso de erro de validação de ponteiro ou falha de carregamento, a thread termina com `exit(-1)`

#### Resultados de Testes
- ✅ `userprog/halt`: Desliga o sistema — testa se a syscall de desligamento encerra corretamente o kernel.
- ❌ `userprog/exit`: Exit básico imprime status — valida que o processo finaliza e reporta o status de saída.

- ❌ `userprog/exec-once`: Execução simples de um processo — carrega e executa um único programa.
- ❌ `userprog/exec-arg`: Exec com argumentos — testa se `exec()` aceita e passa a string de comando corretamente.
- ❌ `userprog/exec-bound`: Exec com argumento no limite de tamanho — verifica limites de tamanho do comando.
- ❌ `userprog/exec-bound-2`: Variante de limite de `exec()` — testa limites de buffer/heap ao executar.
- ❌ `userprog/exec-bound-3`: Outra variante de limite de `exec()` — casos fronteira adicionais de `exec()`.
- ❌ `userprog/exec-multiple`: Execução de múltiplos programas — cria vários filhos sequencialmente/paralelamente.
- ❌ `userprog/exec-missing`: Exec de programa ausente — `exec()` deve falhar e retornar erro/indicar falha.
- ❌ `userprog/exec-bad-ptr`: `exec()` com ponteiro inválido — validações de ponteiro causam `exit(-1)`.

- ❌ `userprog/wait-simple`: `wait()` em um filho simples — pai aguarda término e recebe status.
- ❌ `userprog/wait-twice`: `wait()` é chamado duas vezes no mesmo filho — testa comportamento e retornos.
- ❌ `userprog/wait-killed`: Espera por filho que foi morto — verifica notificação e status.
- ❌ `userprog/wait-bad-pid`: `wait()` com pid inválido — testa erro no argumento de `wait()`.

### Parte 3 - Interface Geral de Syscalls e Validação de Ponteiros (Método 2: Page Faults)

#### `userprog/syscall.c`
- Adicionadas declarações de funções auxiliares para acesso seguro à memória do usuário: `memcpy_from_user()`, `strncpy_from_user()`, `memcpy_to_user()`, `strncpy_to_user()`
- Modificada `syscall_handler()` para utilizar `memcpy_from_user()` ao extrair argumentos da pilha do usuário em vez de acesso direto
- Implementadas funções `_get_byte_from_user()` e `_put_byte_to_user()` com inline assembly para recuperação de page faults
- Implementadas `memcpy_from_user()` e `memcpy_to_user()` para cópia byte-a-byte com validação de endereços via `is_user_vaddr()`
- Implementadas `strncpy_from_user()` e `strncpy_to_user()` para cópia segura de strings nulo-terminadas com limite de comprimento
- Adicionada inclusão de `<threads/vaddr.h>` para utilização de macros de validação de espaço de usuário

#### `userprog/exception.c`
- Modificado handler de page fault `page_fault()` para implementar Método 2 de validação de ponteiros
- Adicionada verificação: se page fault não ocorreu em modo usuário (`!user`) E o endereço faultado é de usuário (`is_user_vaddr(fault_addr)`), então é um acesso de kernel a memória de usuário inválido
- Implementado mecanismo de recuperação: desvia `eip` para endereço de recuperação (armazenado em `eax`) e sinaliza erro com `eax = 0xffffffff`
- Retorna controle normalmente sem causar kernel panic, permitindo que funções de cópia segura detectem e tratarem o erro

#### Design

**Método de Validação Escolhido: Método 2 (Page Faults)** conforme proposto no guia oficial do projeto PintOS.

1. **Acesso seguro à memória do usuário** (Método 2 - Page Faults):
   - Funções `_get_byte_from_user()` e `_put_byte_to_user()` utilizam inline assembly com tratamento de exceção
   - Ao tentar ler/escrever em endereço inválido de usuário, a CPU dispara um page fault (exceção)
   - O handler de page fault (`page_fault()` em `exception.c`) detecta se o fault ocorreu em modo kernel (`!user`) e o endereço é de usuário (`is_user_vaddr(fault_addr)`)
   - Se detectado, o handler desvia `eip` para o rótulo de recuperação (armazenado em `eax`) e retorna, evitando o kernel panic
   - As funções retornam `false` para indicar falha

2. **Validação de argumentos em syscalls**:
   - Ao receber uma syscall, `syscall_handler()` primeiro valida e extrai o número da syscall usando `memcpy_from_user()`
   - Cada argumento é extraído com validação, retornando `false` em caso de falha
   - Se qualquer argumento for inválido, a thread termina imediatamente com `syscall_exit(-1)`
   - Endereços de ponteiros são validados com `is_user_vaddr()` antes de lê-los

3. **Funções de cópia segura**:
   - `memcpy_from_user()` / `memcpy_to_user()`: Copia byte-a-byte validando cada acesso
   - `strncpy_from_user()` / `strncpy_to_user()`: Copia strings nulo-terminadas com limite de comprimento
   - Todas mantêm invariantes de validação e retornam booleano indicando sucesso/falha

#### Resultados de Testes
- ✅ `userprog/sc-bad-sp`: Validação de stack pointer inválido — acesso a memória não mapeada 64MB abaixo do limite válido.
- ✅ `userprog/sc-bad-arg`: Validação de argumentos fora do espaço de usuário — número de syscall válido, mas argumento localizado acima do limite de memória do usuário.
- ✅ `userprog/sc-boundary`: Syscall dividido entre páginas — número de syscall e argumento em páginas diferentes (ambas válidas).
- ✅ `userprog/sc-boundary-2`: Bytes de syscall parcialmente inválidos — primeiro byte do número em memória válida, bytes restantes fora do espaço de usuário.
- ✅ `userprog/sc-boundary-3`: Número de syscall no limite do BSS — posicionado na fronteira final da memória alocada.

- ❌ `userprog/bad-read`: `read()` com ponteiro inválido — garante que leituras em endereços inválidos abortem o processo.
- ❌ `userprog/bad-write`: `write()` com ponteiro inválido — valida proteção contra gravações em memória não mapeada.
- ❌ `userprog/bad-read2`: Segunda variante de `bad-read` — casos adicionais de ponteiro de leitura inválido.
- ❌ `userprog/bad-write2`: Segunda variante de `bad-write` — casos adicionais de ponteiro de escrita inválido.
- ❌ `userprog/bad-jump`: Salto para endereço inválido — testa proteção contra saltos para código não mapeado.
- ❌ `userprog/bad-jump2`: Outra variante de salto inválido — caso fronteira de execução insegura.

### Parte 4 - Chamadas de Sistema de Arquivo
#### `userprog/syscall.c`, `filesys/file.c`, `filesys/inode.c`
- Implementadas syscalls de arquivos: `create()`, `remove()`, `open()`, `filesize()`, `read()`, `write()`, `seek()`, `tell()`, `close()`
- Modificada a estrutura de cada thread para manter uma tabela de file descriptors (`struct file_descriptor`)
- Adicionada sincronização de acesso ao sistema de arquivos usando locks globais
- Delegadas operações de arquivos para funções do subsistema `filesys`

#### Design
1. Ao chamar `open()`, o arquivo é aberto e um novo file descriptor único é atribuído à thread, ou retorna -1 em caso de erro
2. As syscalls `read()` e `write()` validam os buffers de usuário e delegam a leitura/escrita para `file_read()` e `file_write()`
3. `seek()` e `tell()` manipulam a posição de leitura/escrita do arquivo associado ao file descriptor
4. `close()` remove o file descriptor da tabela da thread e fecha o arquivo correspondente
5. Todas as operações de arquivos são protegidas por um lock global para garantir acesso concorrente seguro

#### Resultados de Testes
- ❌ `userprog/create-normal`: Criação normal de arquivo — testa `create()` com nome válido e tamanho.
- ❌ `userprog/create-empty`: `create()` com nome vazio — verifica comportamento para nomes inválidos/vazios.
- ❌ `userprog/create-null`: `create()` com ponteiro NULL — valida checagem de ponteiro de nome.
- ❌ `userprog/create-bad-ptr`: `create()` com ponteiro inválido — deve causar `exit(-1)`.
- ❌ `userprog/create-long`: Nome muito longo em `create()` — testa limites de comprimento de nome.
- ❌ `userprog/create-exists`: Criar arquivo que já existe — deve falhar graciosamente.
- ❌ `userprog/create-bound`: Casos fronteira de `create()` — limites e alinhamentos.

- ❌ `userprog/open-normal`: Abertura normal de arquivo — `open()` retorna fd válido.
- ❌ `userprog/open-missing`: `open()` de arquivo não existente — retorna erro (-1).
- ❌ `userprog/open-boundary`: Abertura em caso fronteira — testes de limites de nome/pointer.
- ❌ `userprog/open-empty`: `open()` com nome vazio — valida checagem de argumento.
- ❌ `userprog/open-null`: `open()` com ponteiro NULL — proteção contra ponteiros inválidos.
- ❌ `userprog/open-bad-ptr`: `open()` com ponteiro inválido — deve abortar o processo.
- ❌ `userprog/open-twice`: Abrir o mesmo arquivo duas vezes — verifica fd distinto ou compartilhamento.

- ❌ `userprog/close-normal`: Fechar fd válido — `close()` libera descriptor.
- ❌ `userprog/close-twice`: Fechar duas vezes o mesmo fd — verfifica falha/segurança.
- ❌ `userprog/close-stdin`: Tentar fechar STDIN — teste de proteção para descritores reservados.
- ❌ `userprog/close-stdout`: Tentar fechar STDOUT — teste de proteção para descritores reservados.
- ❌ `userprog/close-bad-fd`: `close()` com fd inválido — deve retornar erro.

- ❌ `userprog/read-normal`: Leitura de arquivo normal — `read()` retorna bytes corretos.
- ❌ `userprog/read-bad-ptr`: `read()` com buffer inválido — valida checagem de ponteiro de usuário.
- ❌ `userprog/read-boundary`: `read()` em limites de buffer/pilha — casos fronteira.
- ❌ `userprog/read-zero`: `read()` com tamanho zero — deveria retornar 0 sem erro.
- ❌ `userprog/read-stdout`: `read()` de STDOUT — teste de comportamento em descritores não-leitura.
- ❌ `userprog/read-bad-fd`: `read()` com fd inválido — deve retornar erro.

- ❌ `userprog/write-normal`: Escrita normal — `write()` grava e retorna número de bytes.
- ❌ `userprog/write-bad-ptr`: `write()` com buffer inválido — valida proteção de ponteiros de usuário.
- ❌ `userprog/write-boundary`: `write()` em limites de buffer/pilha — casos fronteira.
- ❌ `userprog/write-zero`: `write()` com tamanho zero — deve retornar 0 sem erro.
- ✅ `userprog/write-stdin`: `write()` em STDIN — teste de comportamento em descritor não-escrita.
- ✅ `userprog/write-bad-fd`: `write()` com fd inválido — valida retorno de erro para fd incorreto.

- ❌ `filesys/base/lg-create`: Teste de carga grande para `create()` — cria muitos arquivos para estressar FS.
- ❌ `filesys/base/lg-full`: Criação até encher FS — testa condição de disco cheio.
- ❌ `filesys/base/lg-random`: Teste aleatório de criação/leitura/escrita grande.
- ❌ `filesys/base/lg-seq-block`: Leitura sequencial com blocos grandes.
- ❌ `filesys/base/lg-seq-random`: Leitura sequencial com padrões aleatórios.
- ❌ `filesys/base/sm-create`: Testes pequenos de criação e remoção.
- ❌ `filesys/base/sm-full`: Pequenas criações até encher espaço — caso de encher FS em pequeno cenário.
- ❌ `filesys/base/sm-random`: Testes aleatórios pequenos.
- ❌ `filesys/base/sm-seq-block`: Leitura/escrita sequencial em blocos pequenos.
- ❌ `filesys/base/sm-seq-random`: Sequência com padrões aleatórios em pequeno cenário.
- ❌ `filesys/base/syn-read`: Leitura sincronizada concorrente — testa locks de FS.
- ❌ `filesys/base/syn-remove`: Remoção concorrente de arquivos — sincronização e segurança.
- ❌ `filesys/base/syn-write`: Escrita concorrente no mesmo arquivo — valida locks e atomicidade.

### Parte 5 - Compartilhamento de Descritores e Processos Filhos
#### `userprog/syscall.c`, `threads/thread.c`, `filesys/file.c`
- Implementada herança de file descriptors durante `exec()` para processos filhos
- Adicionada sincronização de acesso concorrente a arquivos com lock global do sistema de arquivos
- Estruturada lista de gerenciamento de filhos (`child_info`) em cada thread para controle de status e sincronização

#### Design
1. Ao executar `exec`, a tabela de file descriptors do processo pai é duplicada para o processo filho, permitindo herança de arquivos abertos
2. Todas as operações de leitura/escrita/fechamento de arquivos utilizam o lock global para evitar condições de corrida
3. Cada thread mantém uma lista de filhos (`child_info`) com status de término e semáforo para sincronização
4. O pai pode chamar `wait()` para aguardar o término de um filho específico, liberando o registro após o término
5. O gerenciamento de recursos garante que file descriptors e estruturas de filhos sejam liberados corretamente ao final do processo

#### Resultados de Testes
- ❌ `userprog/multi-recurse`: Execuções aninhadas de processos — testa `exec()` recursivo e empilhamento de processos.
- ❌ `userprog/multi-child-fd`: Filho herda e compartilha fds — verifica herança e concorrência em arquivos abertos.
- ❌ `userprog/rox-simple`: Regiões de sobreposição (rox) simples — testa concorrência/locks em operações sobrepostas.
- ❌ `userprog/rox-child`: Variante com filho — rox com múltiplos processos acessando o mesmo arquivo.
- ❌ `userprog/rox-multichild`: Variante multi-filho — testa contenção e sincronização entre muitos filhos.

- ❌ `userprog/exec-multiple`: Execução de múltiplos programas (listada também na Parte 2) — testes de criação e término de vários filhos.
- ❌ `userprog/multi-recurse`: (duplicado) Execuções aninhadas — reforça casos de recursão em `exec()`.

- ❌ `userprog/wait-simple`: Espera por filho simples — sincronização pai/filho.
- ❌ `userprog/wait-twice`: Espera duplicada pelo mesmo filho — comportamento ante múltiplas chamadas `wait()`.
- ❌ `userprog/wait-killed`: Espera por filho que foi morto — caso de sinalização e status de término.
- ❌ `userprog/wait-bad-pid`: `wait()` com PID inválido — valida retorno de erro.