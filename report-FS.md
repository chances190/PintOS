# PintOS - Relatório de Implementação

## Projeto 4 - Sistema de Arquivos

### Parte 1: Arquivos Indexados e Extensíveis

#### `src/filesys/inode.c`, `src/filesys/inode.h`

- Modificada: `struct inode_disk` — substituído campo `start` (alocação contígua) por array `blocks[12]` com 10 ponteiros diretos, 1 indireto e 1 duplo-indireto para suportar arquivos de até ~8 MB.
- Adicionado: campo `bool is_dir` em `struct inode_disk` — distingue arquivos de diretórios no disco.
- Adicionado: constantes `DIRECT_BLOCKS` (10), `INDIRECT_BLOCKS` (1), `DOUBLE_INDIRECT_BLOCKS` (1), `PTRS_PER_BLOCK` (128) — definem estrutura de indexação.
- Adicionado: `static struct lock inode_lock` — protege lista `open_inodes` contra acesso concorrente.
- Implementada: `allocate_block()` — aloca e zera um único bloco no disco, retornando setor ou -1 em falha.
- Implementada: `free_block()` — libera bloco individual validando setor não-nulo.
- Implementada: `get_data_block()` — navega estrutura indexada multi-nível e retorna setor do bloco de dados; suporta alocação lazy via parâmetro `allocate`.
- Implementada: `free_inode_blocks()` — libera recursivamente todos os blocos de um inode (diretos, indiretos e duplo-indiretos).
- Modificada: `byte_to_sector()` — usa `get_data_block()` para navegação indexada em vez de offset contíguo.
- Modificada: `inode_init()` — inicializa `inode_lock` para sincronização.
- Modificada: `inode_create()` — aloca blocos usando estrutura indexada, define `is_dir = false`.
- Modificada: `inode_open()` — adquire/libera `inode_lock` para proteger lista de inodes abertos.
- Modificada: `inode_close()` — usa `inode_lock` e chama `free_inode_blocks()` ao deletar inode.
- Modificada: `inode_write_at()` — estende arquivo dinamicamente alocando novos blocos e atualizando `length`.
- Implementada: `inode_create_dir()` — cria inode de diretório com `is_dir = true` e `length = 0`.
- Implementada: `inode_is_dir()` — retorna `true` se inode representa diretório.
- Implementada: `inode_is_removed()` — retorna `true` se inode foi marcado para remoção.

#### `src/filesys/Make.vars`

- Modificado: habilitado suporte a VM (`kernel.bin: DEFINES += -DVM`, `KERNEL_SUBDIRS += vm`).

#### Design

**Estrutura de Indexação Multi-Nível:**

A estrutura `inode_disk` foi redesenhada para eliminar fragmentação externa e suportar arquivos de até ~8 MB:

```c
struct inode_disk {
    block_sector_t blocks[12];  // [0-9]: diretos, [10]: indireto, [11]: duplo-indireto
    off_t length;
    bool is_dir;
    unsigned magic;
    uint32_t unused[113];
};
```

**Capacidade de Armazenamento:**
- 10 blocos diretos: 10 × 512 bytes = 5 KB
- 1 bloco indireto: 128 × 512 bytes = 64 KB
- 1 bloco duplo-indireto: 128 × 128 × 512 bytes = 8 MB
- **Total: ~8 MB**

**Algoritmo de Navegação em `get_data_block()`:**

1. **Blocos Diretos** (índice < 10):
   - Acesso direto ao array `blocks[index]`
   - Se bloco não alocado e `allocate == true`: aloca novo bloco

2. **Bloco Indireto** (índice 10-137):
   - Lê tabela de índices do `blocks[10]`
   - Calcula offset: `indirect_idx = index - 10`
   - Retorna setor em `indirect_table[indirect_idx]`
   - Aloca blocos conforme necessário (tabela de índices e bloco de dados)

3. **Bloco Duplo-Indireto** (índice 138-16521):
   - Lê tabela de índices de primeiro nível do `blocks[11]`
   - Calcula índices: `first_level = (index - 138) / 128`, `second_level = (index - 138) % 128`
   - Lê tabela de segundo nível do `first_level_table[first_level]`
   - Retorna setor em `second_level_table[second_level]`
   - Aloca até 3 blocos se necessário (duas tabelas + bloco de dados)

**Extensão de Arquivo em `inode_write_at()`:**

1. Verifica se escrita ultrapassa EOF: `offset + size > length`
2. Calcula setores necessários: `new_sectors = bytes_to_sectors(offset + size)`
3. Itera de `old_sectors` até `new_sectors` chamando `get_data_block(i, true)` para alocar
4. Se alocação falhar: retorna bytes escritos até então
5. Atualiza `inode->data.length` com novo tamanho
6. Escreve inode atualizado de volta ao disco com `block_write()`
7. Gaps entre EOF antigo e nova posição de escrita são automaticamente zerados por `allocate_block()`

**Suporte a Sparse Files:**

Implementação de alocação lazy: blocos só são alocados quando efetivamente escritos. Leituras de posições não alocadas entre EOF e write position retornam zeros via bounce buffer zerado, sem acesso a disco.

**Liberação Recursiva de Blocos:**

A função `free_inode_blocks()` percorre toda hierarquia de índices:
1. Libera 10 blocos diretos diretamente
2. Lê tabela indireta, libera 128 blocos de dados, depois libera a tabela
3. Lê tabela duplo-indireta, para cada entrada:
   - Lê tabela de segundo nível
   - Libera até 128 blocos de dados
   - Libera tabela de segundo nível
4. Libera tabela duplo-indireta

**Sincronização de Inodes:**

- `inode_lock` protege a lista `open_inodes` contra condições de corrida
- Lock adquirido em `inode_open()` e `inode_close()` ao manipular a lista
- Garante que múltiplas threads não corrompam a estrutura de inodes abertos

#### Resultados de Testes

- ✅ `filesys/extended/grow-create`: Criar arquivo e estendê-lo.
- ✅ `filesys/extended/grow-dir-lg`: Crescer diretório grande.
- ✅ `filesys/extended/grow-file-size`: Verificar tamanho após crescimento.
- ✅ `filesys/extended/grow-root-lg`: Crescer diretório raiz grande.
- ✅ `filesys/extended/grow-root-sm`: Crescer diretório raiz pequeno.
- ✅ `filesys/extended/grow-seq-lg`: Escrita sequencial grande (~8MB).
- ✅ `filesys/extended/grow-seq-sm`: Escrita sequencial pequena.
- ✅ `filesys/extended/grow-sparse`: Arquivo esparso com gaps.
- ✅ `filesys/extended/grow-tell`: `tell()` após extensão.
- ✅ `filesys/extended/grow-two-files`: Dois arquivos crescendo simultaneamente.
- ✅ `filesys/extended/grow-create-persistence`: Persistência após crescimento.
- ✅ `filesys/extended/grow-dir-lg-persistence`: Persistência de diretório grande.
- ✅ `filesys/extended/grow-file-size-persistence`: Persistência de tamanho.
- ✅ `filesys/extended/grow-root-lg-persistence`: Persistência de root grande.
- ✅ `filesys/extended/grow-root-sm-persistence`: Persistência de root pequeno.
- ✅ `filesys/extended/grow-seq-lg-persistence`: Persistência de escrita sequencial grande.
- ✅ `filesys/extended/grow-seq-sm-persistence`: Persistência de escrita sequencial pequena.
- ✅ `filesys/extended/grow-sparse-persistence`: Persistência de arquivo esparso.
- ✅ `filesys/extended/grow-tell-persistence`: Persistência de position após tell.
- ✅ `filesys/extended/grow-two-files-persistence`: Persistência de dois arquivos.

### Parte 2: Subdiretórios

#### `src/filesys/directory.c`, `src/filesys/directory.h`

- Modificada: `struct dir` — mantém `struct inode *inode` e `off_t pos` para posição de leitura.
- Modificada: `dir_create()` — cria diretório com entradas "." (auto-referência) e ".." (pai, inicialmente apontando para si mesmo).
- Modificada: `dir_lookup()` — verifica se inode foi removido antes de retorná-lo via `inode_is_removed()`.
- Modificada: `dir_remove()` — previne remoção de "." e ".."; verifica se diretório está vazio antes de permitir remoção.
- Implementada: `get_starting_directory()` — retorna diretório raiz para caminhos absolutos ou cwd para relativos.
- Implementada: `navigate_to_parent()` — navega para diretório pai via entrada "..".
- Implementada: `navigate_to_subdirectory()` — navega para subdiretório validando que é um diretório via `inode_is_dir()`.
- Implementada: `navigate_path_component()` — processa um componente de caminho (".", "..", ou nome).
- Implementada: `dir_parse_path()` — parseia caminho completo retornando diretório pai e nome do componente final.
- Implementada: `dir_lookup_path()` — wrapper de `dir_parse_path()` com validação de tamanho de nome.
- Implementada: `dir_set_parent()` — atualiza entrada ".." de um diretório filho para apontar ao pai correto.
- Implementada: `dir_set_pos()` — define posição de leitura do diretório.
- Implementada: `dir_get_pos()` — retorna posição atual de leitura do diretório.

#### `src/filesys/filesys.c`

- Modificada: `filesys_create()` — usa `dir_lookup_path()` para resolver caminhos; não permite criar arquivo com nome vazio.
- Modificada: `filesys_open()` — usa `dir_lookup_path()` para resolver caminhos; abre diretório se nome final vazio.
- Modificada: `filesys_remove()` — usa `dir_lookup_path()` para resolver caminhos; não permite remover root ou nome vazio.

#### `src/userprog/syscall.c`

- Implementada: `validate_and_copy_path()` — copia caminho de usuário para kernel com validação.
- Implementada: `parse_directory_path()` — wrapper para `dir_lookup_path()` usado em syscalls.
- Implementada: `resolve_directory_from_path()` — resolve caminho completo para um diretório.
- Implementada: `syscall_chdir()` — muda diretório de trabalho atual do processo.
- Implementada: `syscall_mkdir()` — cria novo diretório com alocação de inode, criação de "."/"..".
- Implementada: `syscall_readdir()` — lê próxima entrada de diretório pulando "." e "..".
- Implementada: `syscall_isdir()` — verifica se fd refere a diretório.
- Implementada: `syscall_inumber()` — retorna número do inode de um fd.
- Modificada: `syscall_open()` — suporta abertura de diretórios.
- Modificada: `syscall_create()` — suporta caminhos com diretórios.
- Modificada: `syscall_remove()` — suporta remoção de diretórios vazios.
- Adicionado: `DEBUG_PRINT` em várias funções para depuração condicional.

#### `src/threads/thread.h`, `src/threads/thread.c`

- Adicionado: campo `struct dir *cwd` em `struct thread` — diretório de trabalho atual do processo.
- Modificada: `init_thread()` — inicializa `cwd` como NULL.
- Modificada: `thread_create()` — herda `cwd` do processo pai via `dir_reopen()`.

#### `src/userprog/process.c`

- Modificada: `process_exit()` — fecha `cwd` se não for NULL via `dir_close()`.
- Modificada: `start_process()` — inicializa `cwd` como diretório raiz para o primeiro processo.

#### Design

**Resolução de Caminhos:**

O sistema suporta caminhos absolutos (começando com "/") e relativos (baseados em cwd):

1. `get_starting_directory()` determina ponto de partida:
   - Caminho absoluto: retorna `dir_open_root()`
   - Caminho relativo: retorna `dir_reopen(thread_current()->cwd)` ou root se cwd for NULL

2. `dir_parse_path()` processa o caminho:
   - Tokeniza por "/" usando `strtok_r()`
   - Para cada componente exceto o último, navega via `navigate_path_component()`
   - Retorna diretório pai e nome do componente final

3. Componentes especiais:
   - "." → permanece no diretório atual
   - ".." → navega para pai via entrada ".."

**Criação de Diretórios (`mkdir`):**

1. Parseia caminho para obter diretório pai e nome
2. Aloca setor via `free_map_allocate()`
3. Cria inode de diretório via `inode_create_dir()`
4. Adiciona entradas "." e ".." via `dir_add()`
5. Atualiza ".." para apontar ao pai correto via `dir_set_parent()`
6. Adiciona entrada no diretório pai via `dir_add()`

**Remoção de Diretórios:**

1. Não permite remover "." ou ".."
2. Verifica se diretório está vazio (apenas "." e ".." como entradas)
3. Marca inode para remoção
4. Inode só é liberado quando último opener fecha

**Herança de cwd:**

- Processo filho herda cwd do pai em `thread_create()` via `dir_reopen()`
- Garante que pai e filho tenham referências independentes ao mesmo diretório
- `dir_close()` em `process_exit()` libera a referência

**Tratamento de Diretório Removido:**

- `inode_is_removed()` verifica se inode foi marcado para remoção
- `dir_lookup()` retorna NULL se inode do resultado foi removido
- `navigate_path_component()` falha se diretório atual foi removido
- Impede navegação através de diretórios deletados

#### Resultados de Testes

- ✅ `filesys/extended/dir-empty-name`: Criar/abrir com nome vazio falha.
- ✅ `filesys/extended/dir-mk-tree`: Criar árvore de diretórios.
- ✅ `filesys/extended/dir-mkdir`: Criar diretório simples.
- ✅ `filesys/extended/dir-open`: Abrir diretório como arquivo.
- ✅ `filesys/extended/dir-over-file`: Não criar diretório sobre arquivo.
- ✅ `filesys/extended/dir-rm-cwd`: Remover diretório de trabalho atual.
- ✅ `filesys/extended/dir-rm-parent`: Remover diretório pai.
- ✅ `filesys/extended/dir-rm-root`: Não remover diretório raiz.
- ✅ `filesys/extended/dir-rm-tree`: Remover árvore de diretórios.
- ✅ `filesys/extended/dir-rmdir`: Remover diretório simples.
- ✅ `filesys/extended/dir-under-file`: Não criar diretório sob arquivo.
- ✅ `filesys/extended/dir-vine`: Cadeia profunda de diretórios.
- ✅ `filesys/extended/dir-empty-name-persistence`: Persistência de nome vazio.
- ✅ `filesys/extended/dir-mk-tree-persistence`: Persistência de árvore.
- ✅ `filesys/extended/dir-mkdir-persistence`: Persistência de mkdir.
- ✅ `filesys/extended/dir-open-persistence`: Persistência de abertura.
- ✅ `filesys/extended/dir-over-file-persistence`: Persistência de dir sobre arquivo.
- ✅ `filesys/extended/dir-rm-cwd-persistence`: Persistência de remoção de cwd.
- ✅ `filesys/extended/dir-rm-parent-persistence`: Persistência de remoção de pai.
- ✅ `filesys/extended/dir-rm-root-persistence`: Persistência de tentativa de remoção de root.
- ✅ `filesys/extended/dir-rm-tree-persistence`: Persistência de remoção de árvore.
- ✅ `filesys/extended/dir-rmdir-persistence`: Persistência de rmdir.
- ✅ `filesys/extended/dir-under-file-persistence`: Persistência de dir sob arquivo.
- ✅ `filesys/extended/dir-vine-persistence`: Persistência de cadeia profunda.

### Parte 3: Sincronização do Sistema de Arquivos

#### `src/filesys/inode.c`

- Adicionado: `static struct lock inode_lock` — lock global para proteger lista de inodes abertos.
- Modificada: `inode_init()` — inicializa `inode_lock`.
- Modificada: `inode_open()` — adquire lock ao buscar/adicionar inode na lista.
- Modificada: `inode_close()` — adquire lock ao decrementar contador e remover da lista.

#### `src/userprog/syscall.c`

- Modificada: handler de `SYS_READDIR` — copia resultado para espaço de kernel primeiro, depois para usuário.
- Modificada: `syscall_readdir()` — preenche buffer kernel, sincroniza posição via `dir_set_pos()`/`file_seek()`.

#### `src/userprog/exception.c`

- Modificada: `page_fault()` — implementado tratamento híbrido de page faults durante syscalls.
- Adicionado: suporte a lazy loading para falhas de kernel em endereços de usuário.
- Adicionado: validação de crescimento de stack com limite de 8 MB (`MAX_STACK_SIZE`).
- Adicionado: fallback para trampoline eax quando lazy loading não é aplicável.

#### `src/threads/thread.h`

- Adicionado: campo `void *user_esp` — salva ESP do usuário durante syscalls para validação de stack growth.

#### Design

**Sincronização de Inodes:**

A lista `open_inodes` é protegida por um lock dedicado (`inode_lock`):
- Adquirido em `inode_open()` ao iterar e modificar a lista
- Adquirido em `inode_close()` ao decrementar `open_cnt` e potencialmente remover
- Liberado antes de operações de I/O para evitar contenção excessiva

**Tratamento de Page Faults durante Syscalls:**

Quando o kernel acessa memória de usuário durante uma syscall e ocorre page fault:

1. Se endereço está no espaço de usuário e `not_present`:
   - Tenta lazy loading via SPT lookup + `vm_load()`
   - Tenta stack growth se dentro da janela válida (32 bytes abaixo de ESP, máximo 8 MB)
   - Se bem-sucedido, retorna e retry da instrução

2. Se lazy loading falha:
   - Usa trampoline eax para sinalizar erro ao syscall handler
   - Syscall retorna erro graciosamente em vez de crashar

**Limite de Stack (8 MB):**

- Definido `MAX_STACK_SIZE` como 8 MB (8 × 1024 × 1024 bytes)
- Stack growth só permitido se `fault_addr >= PHYS_BASE - MAX_STACK_SIZE`
- Previne que endereços arbitrários (como 0x20101234) sejam tratados como stack

**Fluxo de `readdir`:**

1. Kernel abre handle de diretório via `dir_open(inode_reopen())`
2. Sincroniza posição do diretório com posição do arquivo
3. Lê entrada pulando "." e ".." para buffer kernel
4. Atualiza posição do arquivo via `file_seek()`
5. Handler copia resultado para espaço de usuário via `memcpy_to_user()`

#### Resultados de Testes

- ✅ `filesys/extended/syn-rw`: Leitura/escrita concorrente.
- ✅ `filesys/extended/syn-rw-persistence`: Persistência após operações concorrentes.
- ✅ `filesys/base/syn-read`: Leitura concorrente básica.
- ✅ `filesys/base/syn-remove`: Remoção durante leitura.
- ✅ `filesys/base/syn-write`: Escrita concorrente básica.

### Parte 4: Correção de Bugs

**`src/userprog/process.c`:**
- Corrigido: `process_exit()` — garante que `exit_status` é definido antes de verificar se é órfão, e `process_info` é liberado após imprimir status de saída.

**`src/userprog/exception.c`:**
- Corrigido: tratamento híbrido de page faults — primeiro tenta lazy loading/stack growth, depois fallback para eax trampoline.
- Corrigido: validação de stack growth com limite de 8 MB para rejeitar endereços inválidos.


