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
- ✅ `userprog/args-none`: Sem argumentos — testa a execução de um programa sem parâmetros.
- ✅ `userprog/args-single`: Um argumento — valida passagem e leitura de um único argumento.
- ✅ `userprog/args-multiple`: Múltiplos argumentos — verifica parsing e empilhamento de vários argumentos.
- ✅ `userprog/args-many`: Muitos argumentos — testa limites do número de argumentos aceitos pela pilha.
- ✅ `userprog/args-dbl-space`: Espaços duplos tratados corretamente — garante que espaços extras não criem argumentos vazios.

### Parte 2 - Controle de Processos (halt, exit, exec, wait)

#### `threads/thread.h`
- Definida estrutura `process_exec_status` para rastrear status de processos filhos
- Adicionado campo `struct list children` em `struct thread` — lista de estruturas `process_exec_status` dos filhos
- Adicionado campo `struct process_exec_status *exec_status` em `struct thread` — ponteiro para estrutura compartilhada com pai

#### `threads/thread.c`
- Modificado `thread_create()` para alocar e inicializar `process_exec_status` do novo processo
- Modificado `init_thread()` para inicializar lista `children` vazia

#### `userprog/syscall.c`
- Implementada `syscall_halt()` — chama `shutdown_power_off()` para desligar sistema
- Implementada `syscall_exit()` — salva status em `exec_status`, imprime mensagem de saída, chama `thread_exit()`
- Implementada `syscall_exec()` — valida string do comando, chama `process_execute()`, retorna PID ou erro
- Implementada `syscall_wait()` — delega para `process_wait()` passando PID do filho
- Adicionados casos correspondentes em `syscall_handler()` para despachar as syscalls

#### `userprog/process.c`
- **`process_execute()`**: Alterado retorno de `tid_t` para `pid_t`; aloca `process_exec_status` e adiciona à lista `children` do pai antes de criar thread
- **`process_wait()`**: Reimplementado para buscar filho por PID na lista `children`; valida se já teve `wait()` chamado; bloqueia em semáforo até filho terminar; retorna exit status e libera estrutura
- **`process_exit()`**: Salva exit status; verifica se é órfão para decidir liberar estrutura ou sinalizar pai via semáforo; marca todos os filhos como órfãos e libera estruturas se necessário

#### Design
1. **Estrutura de Dados** (`threads/thread.h`):
   ```c
   struct process_exec_status {
     pid_t pid;                    // ID do processo filho
     int exit_status;              // Status de saída (-1 se killed)
     bool has_exited;              // True se processo já terminou
     bool waited_on;               // True se pai já chamou wait()
     bool orphan;                  // True se pai já terminou
     struct lock lock;             // Protege acessos concorrentes
     struct semaphore wait_sema;   // Pai bloqueia aqui até child exit
     struct list_elem elem;        // Link para lista children do pai
   };
   ```

2. **exec() - `syscall_exec()` e `process_execute()`**:
   1. Valida string do filename com `strncpy_from_user()`
   2. Aloca `process_exec_status` e adiciona à lista `children` do pai
   3. Cria nova thread com `thread_create()`, passando `exec_string` completa
   4. Filho inicializa seu `exec_status` apontando para estrutura compartilhada
   5. Retorna `pid` (igual ao `tid`) ou `PID_ERROR` em caso de falha

3. **wait() - `process_wait()`**:
   1. Busca `process_exec_status` com pid correspondente na lista `children`
   2. Se não encontrado ou já teve `wait()` chamado: retorna -1
   3. Marca `waited_on = true` para prevenir múltiplos waits
   4. Chama `sema_down(&child_stat->wait_sema)` - **bloqueia até child terminar**
   5. Quando desbloqueado: lê `exit_status`, remove da lista, libera estrutura
   6. Retorna `exit_status` do filho

4. **exit() - `syscall_exit()` e `process_exit()`**:
   1. Thread salva `exit_status` em `exec_status->exit_status`
   2. Imprime mensagem: `printf("%s: exit(%d)\n", name, status)`
   3. Adquire lock da estrutura para checar se pai ainda existe (`orphan`)
   4. Se `orphan == true`: libera própria estrutura (pai já foi embora)
   5. Se `orphan == false`: chama `sema_up(&exec_status->wait_sema)` - **desbloqueia pai**
   6. Marca todos os filhos como órfãos e libera estruturas se já terminaram
   7. Fecha todos os file descriptors, destrói page directory, termina thread

5. **halt()**: Chama `shutdown_power_off()` para desligar o sistema

6. **Race Conditions Tratadas**:
   - ✅ Pai chama wait() antes do filho terminar: Pai bloqueia até sinal
   - ✅ Filho termina antes do pai chamar wait(): Status salvo, wait retorna imediatamente
   - ✅ Pai termina antes do filho: Filho vira órfão, libera própria estrutura
   - ✅ Múltiplos waits no mesmo filho: Segundo wait retorna -1
   - ✅ Wait em PID inválido: Retorna -1

#### Resultados de Testes
- ✅ `userprog/halt`: Desliga o sistema — testa se a syscall de desligamento encerra corretamente o kernel.
- ✅ `userprog/exit`: Exit básico imprime status — valida que o processo finaliza e reporta o status de saída.

- ✅ `userprog/exec-once`: Execução simples de um processo — carrega e executa um único programa.
- ✅ `userprog/exec-arg`: Exec com argumentos — testa se `exec()` aceita e passa a string de comando corretamente.
- ✅ `userprog/exec-bound`: Exec com argumento no limite de tamanho — verifica limites de tamanho do comando.
- ✅ `userprog/exec-bound-2`: Variante de limite de `exec()` — testa limites de buffer/heap ao executar.
- 🟡 `userprog/exec-bound-3`: Outra variante de limite de `exec()` — casos fronteira adicionais de `exec()`. (implementado, não testado)
- ✅ `userprog/exec-multiple`: Execução de múltiplos programas — cria vários filhos sequencialmente/paralelamente.
- ✅ `userprog/exec-missing`: Exec de programa ausente — `exec()` deve falhar e retornar erro/indicar falha.
- ✅ `userprog/exec-bad-ptr`: `exec()` com ponteiro inválido — validações de ponteiro causam `exit(-1)`.

- ✅ `userprog/wait-simple`: `wait()` em um filho simples — pai aguarda término e recebe status.
- ✅ `userprog/wait-twice`: `wait()` é chamado duas vezes no mesmo filho — segunda chamada retorna -1.
- ✅ `userprog/wait-killed`: Espera por filho que foi morto — retorna -1 corretamente.
- ✅ `userprog/wait-bad-pid`: `wait()` com pid inválido — retorna -1 imediatamente.

- ✅ `userprog/multi-recurse`: Execuções aninhadas de processos — testa `exec()` recursivo e empilhamento de processos.

### Parte 3 - Interface Geral de Syscalls e Validação de Ponteiros (Método 2: Page Faults)

#### `userprog/syscall.c`
- Implementadas funções `_get_byte_from_user()` e `_put_byte_to_user()` com inline assembly para recuperação de page faults
- Implementada função `memcpy_from_user()` copiando byte-a-byte com validação `is_user_vaddr()` em cada acesso
- Implementada função `strncpy_from_user()` para cópia segura de strings nulo-terminadas com limite `max_len`
- Implementada função `memcpy_to_user()` gravando byte-a-byte com validação de endereço de usuário
- Implementada função `strncpy_to_user()` para escrita segura de strings em espaço de usuário
- Modificado `syscall_handler()` para validar e extrair argumentos usando `memcpy_from_user()` em vez de acesso direto
- Adicionada chamada a `syscall_exit(-1)` quando validação de argumentos falha

#### `userprog/exception.c`
- Modificado handler `page_fault()` para detectar acessos do kernel a memória de usuário inválida
- Adicionada verificação: se `!user && is_user_vaddr(fault_addr)` então é acesso kernel inválido
- Implementado mecanismo de recuperação: desvia `eip` para endereço em `eax` e sinaliza erro com `eax = 0xffffffff`
- Configurado retorno de controle sem kernel panic, permitindo funções de cópia detectarem erro

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

---

## Funcionalidades NÃO Implementadas

### Chamadas de Sistema de Arquivo

**Status**: ❌ **Não implementado**

Syscalls pendentes: `create()`, `remove()`, `open()`, `filesize()`, `read()`, `write()`, `seek()`, `tell()`, `close()`

**Infraestrutura preparada**:
- Campo `struct file *fd_table[FD_TABLE_SIZE]` já existe em `struct thread` (`threads/thread.h`)
- Definido `FD_TABLE_SIZE` como 128

**O que falta**:
- Implementar funções auxiliares em `process.c`: `process_add_file()`, `process_get_file()`, `process_close_file()`
- Implementar as syscalls em `syscall.c`
- Adicionar sincronização global com lock para filesystem
- Modificar `process_exit()` para fechar fds abertos
- Implementar deny-write para executáveis (`file_deny_write()` em `load()`)

#### Testes Pendentes
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
- ❌ `userprog/write-bad-fd`: `write()` com fd inválido — valida retorno de erro para fd incorreto.

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

### Out of Memory (OOM) Handling

**Status**: ❌ **Não implementado**

**O que falta**:
- Tratamento correto de falha de alocação de memória em `process_execute()` e `load()`
- Retornar erro apropriado quando não há memória suficiente para criar processo

#### Testes Pendentes
- ❌ `userprog/no-vm/multi-oom`: Múltiplos processos até OOM — testa comportamento quando memória física se esgota.

### Deny Writes to Executables (Read-Only Executables)

**Status**: ❌ **Não implementado**

**O que falta**:
- Chamar `file_deny_write()` no arquivo executável em `load()` após abrir
- Armazenar ponteiro do arquivo executável em novo campo `exec_file` em `struct thread`
- Em `process_exit()`, chamar `file_allow_write()` e fechar o `exec_file`

#### Testes Pendentes
- ❌ `userprog/multi-child-fd`: Filho tenta acessar fd do pai — verifica isolamento correto.
- ❌ `userprog/rox-simple`: Read-only executable simples — previne escrita em executável em execução.
- ❌ `userprog/rox-child`: Read-only executable com filho — múltiplos processos não podem escrever no executável.
- ❌ `userprog/rox-multichild`: Read-only executable multi-filho — testa proteção com muitos processos simultâneos.