# PintOS - Relatório de Implementação

## Projeto 4 - Sistema de Arquivos

### Parte 1: Arquivos Indexados e Extensíveis (Indexed and Extensible Files)

#### `src/filesys/inode.c`, `src/filesys/inode.h`

- Modificada `struct inode_disk` para usar array de 12 ponteiros de blocos (10 diretos, 1 indireto, 1 duplo-indireto) em vez de `start` contíguo
- Adicionado campo `bool is_dir` em `struct inode_disk` para distinguir arquivos de diretórios
- Definidas constantes `DIRECT_BLOCKS` (10), `INDIRECT_BLOCKS` (1), `DOUBLE_INDIRECT_BLOCKS` (1), `PTRS_PER_BLOCK` (128)
- Implementada `allocate_block()` para alocar e zerar um único bloco no disco, retornando setor ou -1 em falha
- Implementada `free_block()` para liberar um bloco individual validando setor não-nulo
- Implementada `get_data_block()` para navegar estrutura indexada multi-nível e retornar setor do bloco de dados
  - Suporta alocação lazy via parâmetro `allocate`
  - Trata blocos diretos, indiretos e duplo-indiretos com leitura/escrita de tabelas de índices
- Implementada `free_inode_blocks()` para liberar recursivamente todos os blocos de um inode (diretos, indiretos e duplo-indiretos)
- Modificada `byte_to_sector()` para usar `get_data_block()` navegando estrutura indexada em vez de offset contíguo
- Modificada `inode_create()` para alocar blocos usando estrutura indexada iterando sobre setores necessários
- Modificada `inode_write_at()` para estender arquivo dinamicamente alocando novos blocos conforme necessário e atualizando `length`
- Modificada `inode_close()` para chamar `free_inode_blocks()` ao deletar inode em vez de `free_map_release()` com tamanho fixo
- Implementada `inode_create_dir()` para criar inode de diretório com flag `is_dir = true`
- Implementada `inode_is_dir()` para verificar se inode representa um diretório

#### `src/filesys/Make.vars`

- Descomentadas linhas para habilitar VM (`kernel.bin: DEFINES += -DVM`, `KERNEL_SUBDIRS += vm`, `GRADING_FILE = Grading.with-vm`)

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
2. Lê tabela indireto, libera 128 blocos de dados, depois libera a tabela
3. Lê tabela duplo-indireto, para cada entrada:
   - Lê tabela de segundo nível
   - Libera até 128 blocos de dados
   - Libera tabela de segundo nível
4. Libera tabela duplo-indireto

**Sincronização:**

Nesta fase, a sincronização é tratada pelo lock global do filesystem já existente. Extensão de arquivo é protegida pois `inode_write_at()` é atômica em relação a outras operações do filesystem.

#### Resultados de Testes

- ✅ `filesys/extended/grow-create`: Criar arquivo e estendê-lo — testa criação e crescimento básico de arquivo.
- ✅ `filesys/extended/grow-dir-lg`: Crescer diretório grande — valida extensão de diretórios até tamanhos significativos.
- ✅ `filesys/extended/grow-file-size`: Verificar tamanho após crescimento — confirma que `file_length()` reflete extensão correta.
- ✅ `filesys/extended/grow-root-lg`: Crescer diretório raiz grande — testa capacidade de expandir root directory.
- ✅ `filesys/extended/grow-root-sm`: Crescer diretório raiz pequeno — validação de extensão moderada do root.
- ✅ `filesys/extended/grow-seq-lg`: Escrita sequencial grande — arquivo cresce até ~8MB com padrão sequencial.
- ✅ `filesys/extended/grow-seq-sm`: Escrita sequencial pequena — crescimento controlado em múltiplos writes.
- ✅ `filesys/extended/grow-sparse`: Arquivo esparso com gaps — escreve em offsets distantes, verifica preenchimento com zeros.
- ✅ `filesys/extended/grow-tell`: `tell()` após extensão — position indicator correto após seeks e writes além de EOF.
- ✅ `filesys/extended/grow-two-files`: Dois arquivos crescendo simultaneamente — valida alocação independente de blocos.
- ❌ `filesys/extended/syn-rw`: Leitura/escrita concorrente — falha por falta de sincronização granular (será implementada em Feature 4).
- ❌ `filesys/extended/grow-create-persistence`: Persistência após crescimento — requer implementação de persistence (Feature 2).
- ❌ `filesys/extended/grow-dir-lg-persistence`: Persistência de diretório grande — aguarda Feature 2.
- ❌ `filesys/extended/grow-file-size-persistence`: Persistência de tamanho — aguarda Feature 2.
- ❌ `filesys/extended/grow-root-lg-persistence`: Persistência de root grande — aguarda Feature 2.
- ❌ `filesys/extended/grow-root-sm-persistence`: Persistência de root pequeno — aguarda Feature 2.
- ❌ `filesys/extended/grow-seq-lg-persistence`: Persistência de escrita sequencial grande — aguarda Feature 2.
- ❌ `filesys/extended/grow-seq-sm-persistence`: Persistência de escrita sequencial pequena — aguarda Feature 2.
- ❌ `filesys/extended/grow-sparse-persistence`: Persistência de arquivo esparso — aguarda Feature 2.
- ❌ `filesys/extended/grow-tell-persistence`: Persistência de position após tell — aguarda Feature 2.
- ❌ `filesys/extended/grow-two-files-persistence`: Persistência de dois arquivos — aguarda Feature 2.

---

## Plano de Implementação

### Estado Atual do Projeto

- ✅ **Projeto 2 (User Programs)**: 100% completo - todos os 64 testes passando
- ✅ **Filesystem Base**: 100% completo - testes 67-79 (13 testes) passando
- ✅ **Feature 1 (Indexed & Extensible Files)**: 100% completo - 10/10 testes grow passando
- ❌ **Filesystem Extended**: 10/46 testes passando (21.7%)

### Visão Geral das Funcionalidades

O Projeto 4 requer a implementação de 4 funcionalidades principais:

1. ✅ **Indexed and Extensible Files** - Arquivos indexados com crescimento dinâmico (**CONCLUÍDO**)
2. ⏳ **Subdirectories** - Sistema de diretórios hierárquico (**PRÓXIMO**)
3. ⏳ **Buffer Cache** - Cache de 64 setores com write-behind e read-ahead
4. ⏳ **File System Synchronization** - Sincronização para acesso concorrente

---

## Feature 2: Subdirectories

### Objetivo

Implementar sistema de diretórios hierárquico completo com suporte a:
- Diretórios aninhados arbitrariamente
- Caminhos absolutos (`/a/b/c`)
- Caminhos relativos (`../a/./b`)
- Entradas especiais `.` (diretório atual) e `..` (diretório pai)

### Arquivos a Modificar

#### `src/threads/thread.h` e `src/threads/thread.c`

**Adicionar em `struct thread`:**
```c
#ifdef FILESYS
    struct dir *cwd;                  // Diretório de trabalho atual
#endif
```

**Modificar `thread_create()`:**
- Herdar `cwd` do processo pai

**Adicionar em `process_exit()`:**
- Fechar `cwd` ao terminar processo

#### `src/filesys/directory.c` e `src/filesys/directory.h`

**Funções a Implementar:**

1. **`dir_create_with_parent(block_sector_t sector, block_sector_t parent_sector)`**
   - Criar diretório com entradas `.` e `..`
   - `.` aponta para o próprio diretório
   - `..` aponta para o diretório pai

2. **`dir_is_empty(struct dir *dir)`**
   - Verificar se diretório contém apenas `.` e `..`
   - Necessário para `remove()` de diretórios

3. **`parse_path(const char *path, char *filename, struct dir **dir)`**
   - Separar caminho em diretório e nome do arquivo
   - Resolver `.`, `..`, `/` e caminhos relativos
   - Retornar diretório pai e nome do arquivo

4. **`dir_lookup_path(const char *path)`**
   - Navegar caminho completo e retornar inode final
   - Suportar caminhos absolutos e relativos

**Funções auxiliares:**
```c
bool is_absolute_path(const char *path);
struct dir *get_root_dir(void);
struct dir *get_current_dir(void);
bool split_path(const char *path, char **tokens, int *count);
```

#### `src/userprog/syscall.c` e `src/lib/syscall-nr.h`

**Novos System Calls:**

1. **`bool chdir(const char *dir)`**
   - Mudar diretório de trabalho do processo
   - Validar que `dir` existe e é um diretório
   - Atualizar `thread_current()->cwd`

2. **`bool mkdir(const char *dir)`**
   - Criar novo diretório
   - Criar entradas `.` e `..`
   - Falhar se diretório já existe
   - Falhar se caminho intermediário não existe

3. **`bool readdir(int fd, char *name)`**
   - Ler próxima entrada do diretório
   - Pular `.` e `..`
   - Retornar false quando não há mais entradas

4. **`bool isdir(int fd)`**
   - Verificar se fd representa diretório
   - Usar flag `is_dir` do inode

5. **`int inumber(int fd)`**
   - Retornar número do inode (sector number)
   - Válido para arquivos e diretórios

**Modificar System Calls Existentes:**

1. **`open(const char *file)`**
   - Permitir abrir diretórios (além de arquivos)
   - Usar `parse_path()` para resolver caminho

2. **`create(const char *file, unsigned initial_size)`**
   - Usar `parse_path()` para suportar caminhos
   - Criar arquivo no diretório correto

3. **`remove(const char *file)`**
   - Permitir remover diretórios vazios
   - Verificar com `dir_is_empty()`
   - Proibir remoção do diretório raiz
   - **Opcional:** permitir remoção de diretório aberto/em uso

4. **Todos os syscalls de arquivo:**
   - Adicionar suporte a parsing de caminho
   - Validar caminhos absolutos e relativos

#### `src/filesys/filesys.c` e `src/filesys/filesys.h`

**Modificar:**

1. **`filesys_create(const char *name, off_t initial_size)`**
   - Separar caminho e nome do arquivo
   - Criar arquivo no diretório correto

2. **`filesys_open(const char *name)`**
   - Usar `dir_lookup_path()` para resolver caminho

3. **`filesys_remove(const char *name)`**
   - Verificar se é diretório e se está vazio

### Estrutura de Diretórios no Disco

**Diretório Raiz (`/`):**
- Sempre no setor 1
- Não possui `..` válido (ou aponta para si mesmo)

**Formato de Entrada de Diretório:**
```c
struct dir_entry {
    block_sector_t inode_sector;     // Setor do inode
    char name[NAME_MAX + 1];         // Nome do arquivo/diretório (máx 14 chars)
    bool in_use;                     // Entry ativa?
};
```

**Entradas Especiais:**
- `.` - entrada com `inode_sector` apontando para o próprio diretório
- `..` - entrada com `inode_sector` apontando para o diretório pai

### Exemplo de Parsing de Caminho

**Caminho:** `/home/user/../docs/./file.txt`

**Passos:**
1. Começar do root (`/`)
2. Navegar para `home` → encontrar inode de `home`
3. Navegar para `user` → encontrar inode de `user`
4. Processar `..` → voltar para `home`
5. Navegar para `docs` → encontrar inode de `docs`
6. Processar `.` → permanecer em `docs`
7. Arquivo final: `file.txt` no diretório `docs`

### Sincronização

- Lock global do filesystem já existe
- Adicionar locks por diretório se necessário
- Cuidado com deadlocks em operações que envolvem múltiplos diretórios

### Testes Afetados

- `filesys/extended/dir-*` (24 testes)
- Testes de persistência de diretórios

---

## Feature 3: Buffer Cache

### Objetivo

Implementar cache de blocos entre memória e disco para melhorar performance:
- Cache limitado a 64 setores
- Política de eviction LRU (Least Recently Used)
- Write-behind: escrever dirty blocks periodicamente
- Read-ahead: pré-carregar próximo bloco assincronamente

### Arquivos a Criar

#### `src/filesys/cache.c` e `src/filesys/cache.h`

**Estruturas de Dados:**

```c
#define CACHE_SIZE 64

struct cache_entry {
    block_sector_t sector;              // Número do setor (-1 se inválido)
    uint8_t data[BLOCK_SECTOR_SIZE];    // Dados do bloco (512 bytes)
    bool dirty;                         // Necessita write-back?
    bool valid;                         // Entry em uso?
    bool accessed;                      // Para algoritmo LRU (second chance)
    struct lock entry_lock;             // Lock por entrada
    int read_cnt;                       // Número de leitores ativos
    struct condition no_readers;        // Condição para sincronização
};

static struct cache_entry cache[CACHE_SIZE];
static struct lock cache_lock;          // Lock global do cache
static int clock_hand;                  // Para algoritmo clock (LRU aproximado)
```

**Funções Principais:**

1. **`void cache_init(void)`**
   - Inicializar array de cache entries
   - Inicializar locks
   - Criar threads de write-behind e read-ahead

2. **`void cache_read(block_sector_t sector, void *buffer)`**
   - Procurar setor no cache (cache hit)
   - Se encontrado: copiar dados e marcar `accessed = true`
   - Se não encontrado (cache miss):
     - Evocar entrada usando LRU/Clock
     - Ler setor do disco
     - Inserir no cache
     - Copiar dados para buffer

3. **`void cache_write(block_sector_t sector, const void *buffer)`**
   - Procurar setor no cache
   - Se encontrado: atualizar dados, marcar `dirty = true` e `accessed = true`
   - Se não encontrado:
     - Evocar entrada
     - Inserir no cache
     - Marcar `dirty = true`
   - **Write-back:** não escrever imediatamente no disco

4. **`void cache_flush(void)`**
   - Escrever todos os blocos dirty no disco
   - Chamar em `filesys_done()` para persistir ao desligar

5. **`block_sector_t cache_evict(void)`**
   - Implementar algoritmo Clock (Second-Chance LRU):
     - Percorrer cache circularmente
     - Se `accessed = true`: marcar `accessed = false` e continuar
     - Se `accessed = false`: evocar essa entrada
   - Se entrada evocada for dirty, escrever no disco primeiro
   - Retornar índice da entrada evocada

6. **`void cache_readahead(block_sector_t sector)`**
   - Pré-carregar setor de forma assíncrona
   - Não bloquear operação atual

### Integração com Block Device

**Opção A - Modificar `src/devices/block.c`:**
```c
void block_read(struct block *block, block_sector_t sector, void *buffer) {
    cache_read(sector, buffer);  // Usar cache em vez de disco direto
}

void block_write(struct block *block, block_sector_t sector, const void *buffer) {
    cache_write(sector, buffer);  // Usar cache
}
```

**Opção B - Criar wrappers no inode (Recomendada):**
- Manter `block_read/write` intactos
- Inode chama `cache_read/write` diretamente
- Mais controle e menos acoplamento

### Threads Assíncronas

**Write-Behind Thread:**
```c
static void write_behind_thread(void *aux UNUSED) {
    while (true) {
        timer_sleep(WRITE_BEHIND_INTERVAL);  // Ex: 5 segundos
        cache_flush();
    }
}
```

**Read-Ahead Thread:**
```c
static void readahead_thread(void *aux UNUSED) {
    while (true) {
        block_sector_t sector = readahead_queue_pop();  // Fila de pré-leitura
        if (sector != (block_sector_t)-1) {
            uint8_t buffer[BLOCK_SECTOR_SIZE];
            cache_read(sector, buffer);  // Carregar no cache
        }
    }
}
```

### Sincronização

**Desafios:**
- Múltiplos processos acessando mesmo bloco
- Eviction enquanto bloco está sendo lido/escrito
- Write-behind thread vs. operações de escrita

**Solução:**
1. **Lock global (`cache_lock`)**: proteger busca e eviction
2. **Lock por entrada (`entry_lock`)**: proteger leitura/escrita de dados
3. **Readers-writers pattern**: permitir múltiplos leitores ou um escritor

**Ordem de aquisição de locks (para evitar deadlock):**
1. `cache_lock` (encontrar/evocar entrada)
2. `entry_lock` (acessar dados da entrada)
3. Liberar `cache_lock` antes de I/O de disco

### Performance

**Métricas esperadas:**
- Hit rate > 80% em workloads sequenciais
- Redução significativa em chamadas `block_read/write`

### Testes Afetados

- `filesys/extended/syn-rw` - sincronização de leitura/escrita
- Todos os testes se beneficiam de performance melhorada

---

## Feature 4: File System Synchronization

### Objetivo

Garantir corretude em acessos concorrentes ao sistema de arquivos:
- Múltiplos processos lendo/escrevendo mesmo arquivo
- Extensão simultânea de arquivo
- Criação/remoção concorrente de arquivos

### Problemas a Resolver

1. **Race condition em extensão de arquivo:**
   - Processos A e B tentam estender arquivo simultaneamente
   - Solução: lock durante alocação de blocos

2. **Race condition em leitura vs escrita:**
   - Processo A lê enquanto B escreve
   - Requisito: A não pode ler dados corrompidos
   - Solução: sincronização no nível do inode

3. **Fairness:**
   - Leitores não devem bloquear escritores indefinidamente
   - Escritores não devem bloquear leitores indefinidamente

### Estratégias de Sincronização

**Opção A - Lock Global do Filesystem:**
- Um único lock para todas as operações
- **Vantagens:** simples, sem deadlocks
- **Desvantagens:** serializa tudo, baixo paralelismo

**Opção B - Locks Granulares (Recomendada):**
- Lock por inode
- Lock por diretório
- Lock do free_map
- **Vantagens:** alto paralelismo
- **Desvantagens:** mais complexo, risco de deadlock

### Implementação

**Adicionar em `struct inode`:**
```c
struct lock inode_lock;        // Protege extensão e remoção
int readers;                   // Número de leitores ativos
int writers;                   // Número de escritores ativos (0 ou 1)
struct condition can_read;     // Condição para leitores
struct condition can_write;    // Condição para escritores
```

**Funções de Sincronização:**

1. **`inode_lock_read(struct inode *inode)`**
   - Permitir múltiplos leitores
   - Bloquear se há escritor

2. **`inode_unlock_read(struct inode *inode)`**
   - Decrementar leitores
   - Sinalizar escritores se necessário

3. **`inode_lock_write(struct inode *inode)`**
   - Permitir apenas um escritor
   - Bloquear se há leitores ou escritor

4. **`inode_unlock_write(struct inode *inode)`**
   - Liberar escritor
   - Sinalizar próximo leitor/escritor

**Proteger Operações Críticas:**

1. **`inode_write_at()`** - extensão de arquivo:
```c
inode_lock_write(inode);
// Alocar novos blocos
// Atualizar length
inode_unlock_write(inode);
```

2. **`inode_read_at()`**:
```c
inode_lock_read(inode);
// Ler dados
inode_unlock_read(inode);
```

3. **`free_map_allocate()`**:
```c
lock_acquire(&free_map_lock);
// Encontrar e marcar setor livre
lock_release(&free_map_lock);
```

### Prevenir Deadlocks

**Regras de Ordenação:**
1. Nunca adquirir lock de inode enquanto segura lock de diretório
2. Ordem consistente ao adquirir múltiplos locks
3. Evitar locks aninhados quando possível

### Testes Afetados

- `filesys/extended/syn-rw`
- `filesys/extended/grow-two-files`
- Testes de persistence que criam múltiplos arquivos

---

## Ordem de Implementação Recomendada

### Opção A - Stanford Guide (Buffer Cache First)

1. **Buffer Cache** → isolado, não quebra código existente
2. **Indexed & Extensible Files** → modifica estrutura base
3. **Subdirectories** → adiciona funcionalidade final

**Vantagens:**
- Cache pode ser testado independentemente
- Performance melhorada desde o início

**Desvantagens:**
- Lógica complexa antes da base estrutural
- Pode precisar refatorar cache após mudar inode

### Opção B - Lógica Incremental (Recomendada)

1. ✅ **Indexed & Extensible Files** → base estrutural necessária (**CONCLUÍDO**)
2. ⏳ **Subdirectories** → funcionalidade independente do cache (**PRÓXIMO**)
3. ⏳ **Buffer Cache** → otimização final sem modificar lógica
4. ⏳ **File System Sync** → correção de race conditions

**Vantagens:**
- Constrói base sólida primeiro
- Subdirectories funcionam sem cache
- Cache é camada de otimização final

**Desvantagens:**
- Performance só melhora no final

### Decisão: Opção B

**Justificativa:**
- Indexed files são fundamentais para tudo
- Subdirectories testáveis independentemente
- Cache é transparente para resto do código

---

## Estratégia de Branches Git

Cada feature terá sua própria branch para desenvolvimento isolado:

1. ✅ `feat/indexed-files` - Feature 1 (**CONCLUÍDA**)
2. ⏳ `feat/subdirectories` - Feature 2 (**PRÓXIMA**)
3. ⏳ `feat/buffer-cache` - Feature 3
4. ⏳ `feat/fs-sync` - Feature 4 (se necessário como branch separada)

**Workflow:**
1. Criar branch a partir de `main`
2. Implementar feature completa
3. Testar com `make check`
4. Atualizar `report-FS.md` com documentação
5. Merge para `main` após revisão

---

## Cronograma Estimado

| Feature                    | Complexidade | Tempo Estimado | Status      | Testes Passando |
|----------------------------|--------------|----------------|-------------|-----------------|
| Indexed & Extensible Files | Alta         | 6-8 horas      | ✅ Completo | 10/10 (100%)    |
| Subdirectories             | Alta         | 8-10 horas     | ⏳ Próximo  | 0/24 (0%)       |
| Buffer Cache               | Média        | 4-6 horas      | ⏳ Pendente | 0/1 (0%)        |
| File System Sync           | Média        | 2-3 horas      | ⏳ Pendente | N/A             |
| **Total**                  | -            | **20-27h**     | 25% completo| **10/46 (21.7%)**|

---

## Próximos Passos

1. ✅ ~~Revisar e aprovar plano de implementação~~
2. ✅ ~~Escolher primeira feature a implementar (Indexed Files)~~
3. ✅ ~~Criar branch `feat/indexed-files`~~
4. ✅ ~~Implementar Feature 1: Indexed & Extensible Files~~
5. ✅ ~~Testar e documentar Feature 1~~
6. ⏳ Criar branch `feat/subdirectories` para Feature 2
7. ⏳ Implementar Feature 2: Subdirectories
8. ⏳ Repetir para Features 3 e 4

---

### Estado Atual do Projeto

- ✅ **Projeto 2 (User Programs)**: 100% completo - todos os 64 testes passando
- ✅ **Filesystem Base**: 100% completo - testes 67-79 (13 testes) passando
- ❌ **Filesystem Extended**: 0% completo - testes 80-125 (46 testes) falhando

### Visão Geral das Funcionalidades

O Projeto 4 requer a implementação de 4 funcionalidades principais:

1. **Indexed and Extensible Files** - Arquivos indexados com crescimento dinâmico
2. **Subdirectories** - Sistema de diretórios hierárquico
3. **Buffer Cache** - Cache de 64 setores com write-behind e read-ahead
4. **File System Synchronization** - Sincronização para acesso concorrente

---

## Feature 1: Indexed and Extensible Files

### Objetivo

Modificar a estrutura de armazenamento de arquivos para eliminar fragmentação externa e suportar arquivos maiores que 8MB através de indexação multi-nível (direct, indirect, double-indirect blocks).

### Problema Atual

O sistema atual aloca arquivos como uma única extensão contígua (`block_sector_t start`), o que causa:
- Fragmentação externa
- Limite no tamanho máximo de arquivo
- Impossibilidade de crescimento dinâmico

### Solução Proposta

**Estrutura de Indexação Multi-Nível:**
- 10 blocos diretos (direct blocks)
- 1 bloco indireto (indirect block) - aponta para 128 blocos diretos
- 1 bloco duplo-indireto (double-indirect block) - aponta para 128 blocos indiretos

**Capacidade Total:**
- Diretos: 10 × 512 bytes = 5 KB
- Indireto: 128 × 512 bytes = 64 KB
- Duplo-indireto: 128 × 128 × 512 bytes = 8 MB
- **Total: ~8 MB** (suficiente para os requisitos)

### Arquivos a Modificar

#### `src/filesys/inode.c` e `src/filesys/inode.h`

**Modificações em `struct inode_disk`:**
```c
#define DIRECT_BLOCKS 10
#define INDIRECT_BLOCKS 1
#define DOUBLE_INDIRECT_BLOCKS 1
#define TOTAL_BLOCK_PTRS (DIRECT_BLOCKS + INDIRECT_BLOCKS + DOUBLE_INDIRECT_BLOCKS)

struct inode_disk {
    block_sector_t blocks[TOTAL_BLOCK_PTRS];  // Array de ponteiros para blocos
    off_t length;                              // Tamanho do arquivo em bytes
    bool is_dir;                               // Flag: true = diretório, false = arquivo
    unsigned magic;                            // Número mágico
    uint32_t unused[111];                      // Padding para 512 bytes
};
```

**Funções a Implementar/Modificar:**

1. **`inode_create(block_sector_t sector, off_t length, bool is_dir)`**
   - Adicionar parâmetro `is_dir`
   - Alocar blocos conforme necessário usando estrutura indexada
   - Inicializar array de blocos com zeros
   - Para diretórios, criar entradas `.` e `..`

2. **`byte_to_sector(const struct inode *inode, off_t pos)`**
   - Calcular índice do bloco: `block_idx = pos / BLOCK_SECTOR_SIZE`
   - Se `block_idx < 10`: retornar bloco direto
   - Se `block_idx < 10 + 128`: navegar bloco indireto
   - Caso contrário: navegar bloco duplo-indireto

3. **`inode_write_at(struct inode *inode, const void *buffer, off_t size, off_t offset)`**
   - Implementar extensão de arquivo
   - Se escrever além do EOF, alocar novos blocos
   - Preencher gaps com zeros (sparse file support)
   - Atualizar `inode->data.length`

4. **`inode_read_at(struct inode *inode, void *buffer, off_t size, off_t offset)`**
   - Leitura além do EOF retorna 0 bytes
   - Navegar estrutura indexada para localizar dados

5. **`inode_close(struct inode *inode)`**
   - Se arquivo sendo removido, liberar todos os blocos alocados
   - Liberar blocos indiretos e duplo-indiretos recursivamente
   - Atualizar free_map

6. **Funções auxiliares:**
   - `allocate_inode_blocks(struct inode_disk *disk_inode, off_t length)` - alocar blocos necessários
   - `free_inode_blocks(struct inode_disk *disk_inode)` - liberar todos os blocos
   - `get_data_block(const struct inode *inode, block_sector_t block_idx)` - obter setor de um bloco

### Estratégia de Sparse Files

**Opção A - Alocação Imediata:**
- Alocar e zerar todos os blocos até a posição de escrita
- Simples, mas desperdiça espaço em disco

**Opção B - Alocação Lazy (Recomendada):**
- Alocar blocos apenas quando escritos
- Manter flag ou valor especial (ex: `(block_sector_t)-1`) para blocos não alocados
- Leitura de bloco não alocado retorna zeros sem acessar disco

### Sincronização

- Adicionar lock em `struct inode` para proteger extensão de arquivo
- Garantir atomicidade em operações de alocação/liberação de blocos

### Testes Afetados

- `filesys/extended/grow-*` (21 testes)
- Testes de criação, leitura e escrita de arquivos grandes

---

## Feature 2: Subdirectories

### Objetivo

Implementar sistema de diretórios hierárquico completo com suporte a:
- Diretórios aninhados arbitrariamente
- Caminhos absolutos (`/a/b/c`)
- Caminhos relativos (`../a/./b`)
- Entradas especiais `.` (diretório atual) e `..` (diretório pai)

### Arquivos a Modificar

#### `src/threads/thread.h` e `src/threads/thread.c`

**Adicionar em `struct thread`:**
```c
#ifdef FILESYS
    struct dir *cwd;                  // Diretório de trabalho atual
#endif
```

**Modificar `thread_create()`:**
- Herdar `cwd` do processo pai

**Adicionar em `process_exit()`:**
- Fechar `cwd` ao terminar processo

#### `src/filesys/directory.c` e `src/filesys/directory.h`

**Funções a Implementar:**

1. **`dir_create_with_parent(block_sector_t sector, block_sector_t parent_sector)`**
   - Criar diretório com entradas `.` e `..`
   - `.` aponta para o próprio diretório
   - `..` aponta para o diretório pai

2. **`dir_is_empty(struct dir *dir)`**
   - Verificar se diretório contém apenas `.` e `..`
   - Necessário para `remove()` de diretórios

3. **`parse_path(const char *path, char *filename, struct dir **dir)`**
   - Separar caminho em diretório e nome do arquivo
   - Resolver `.`, `..`, `/` e caminhos relativos
   - Retornar diretório pai e nome do arquivo

4. **`dir_lookup_path(const char *path)`**
   - Navegar caminho completo e retornar inode final
   - Suportar caminhos absolutos e relativos

**Funções auxiliares:**
```c
bool is_absolute_path(const char *path);
struct dir *get_root_dir(void);
struct dir *get_current_dir(void);
bool split_path(const char *path, char **tokens, int *count);
```

#### `src/userprog/syscall.c` e `src/lib/syscall-nr.h`

**Novos System Calls:**

1. **`bool chdir(const char *dir)`**
   - Mudar diretório de trabalho do processo
   - Validar que `dir` existe e é um diretório
   - Atualizar `thread_current()->cwd`

2. **`bool mkdir(const char *dir)`**
   - Criar novo diretório
   - Criar entradas `.` e `..`
   - Falhar se diretório já existe
   - Falhar se caminho intermediário não existe

3. **`bool readdir(int fd, char *name)`**
   - Ler próxima entrada do diretório
   - Pular `.` e `..`
   - Retornar false quando não há mais entradas

4. **`bool isdir(int fd)`**
   - Verificar se fd representa diretório
   - Usar flag `is_dir` do inode

5. **`int inumber(int fd)`**
   - Retornar número do inode (sector number)
   - Válido para arquivos e diretórios

**Modificar System Calls Existentes:**

1. **`open(const char *file)`**
   - Permitir abrir diretórios (além de arquivos)
   - Usar `parse_path()` para resolver caminho

2. **`create(const char *file, unsigned initial_size)`**
   - Usar `parse_path()` para suportar caminhos
   - Criar arquivo no diretório correto

3. **`remove(const char *file)`**
   - Permitir remover diretórios vazios
   - Verificar com `dir_is_empty()`
   - Proibir remoção do diretório raiz
   - **Opcional:** permitir remoção de diretório aberto/em uso

4. **Todos os syscalls de arquivo:**
   - Adicionar suporte a parsing de caminho
   - Validar caminhos absolutos e relativos

#### `src/filesys/filesys.c` e `src/filesys/filesys.h`

**Modificar:**

1. **`filesys_create(const char *name, off_t initial_size)`**
   - Separar caminho e nome do arquivo
   - Criar arquivo no diretório correto

2. **`filesys_open(const char *name)`**
   - Usar `dir_lookup_path()` para resolver caminho

3. **`filesys_remove(const char *name)`**
   - Verificar se é diretório e se está vazio

### Estrutura de Diretórios no Disco

**Diretório Raiz (`/`):**
- Sempre no setor 1
- Não possui `..` válido (ou aponta para si mesmo)

**Formato de Entrada de Diretório:**
```c
struct dir_entry {
    block_sector_t inode_sector;     // Setor do inode
    char name[NAME_MAX + 1];         // Nome do arquivo/diretório (máx 14 chars)
    bool in_use;                     // Entry ativa?
};
```

**Entradas Especiais:**
- `.` - entrada com `inode_sector` apontando para o próprio diretório
- `..` - entrada com `inode_sector` apontando para o diretório pai

### Exemplo de Parsing de Caminho

**Caminho:** `/home/user/../docs/./file.txt`

**Passos:**
1. Começar do root (`/`)
2. Navegar para `home` → encontrar inode de `home`
3. Navegar para `user` → encontrar inode de `user`
4. Processar `..` → voltar para `home`
5. Navegar para `docs` → encontrar inode de `docs`
6. Processar `.` → permanecer em `docs`
7. Arquivo final: `file.txt` no diretório `docs`

### Sincronização

- Lock global do filesystem já existe
- Adicionar locks por diretório se necessário
- Cuidado com deadlocks em operações que envolvem múltiplos diretórios

### Testes Afetados

- `filesys/extended/dir-*` (24 testes)
- Testes de persistência de diretórios

---

## Feature 3: Buffer Cache

### Objetivo

Implementar cache de blocos entre memória e disco para melhorar performance:
- Cache limitado a 64 setores
- Política de eviction LRU (Least Recently Used)
- Write-behind: escrever dirty blocks periodicamente
- Read-ahead: pré-carregar próximo bloco assincronamente

### Arquivos a Criar

#### `src/filesys/cache.c` e `src/filesys/cache.h`

**Estruturas de Dados:**

```c
#define CACHE_SIZE 64

struct cache_entry {
    block_sector_t sector;              // Número do setor (-1 se inválido)
    uint8_t data[BLOCK_SECTOR_SIZE];    // Dados do bloco (512 bytes)
    bool dirty;                         // Necessita write-back?
    bool valid;                         // Entry em uso?
    bool accessed;                      // Para algoritmo LRU (second chance)
    struct lock entry_lock;             // Lock por entrada
    int read_cnt;                       // Número de leitores ativos
    struct condition no_readers;        // Condição para sincronização
};

static struct cache_entry cache[CACHE_SIZE];
static struct lock cache_lock;          // Lock global do cache
static int clock_hand;                  // Para algoritmo clock (LRU aproximado)
```

**Funções Principais:**

1. **`void cache_init(void)`**
   - Inicializar array de cache entries
   - Inicializar locks
   - Criar threads de write-behind e read-ahead

2. **`void cache_read(block_sector_t sector, void *buffer)`**
   - Procurar setor no cache (cache hit)
   - Se encontrado: copiar dados e marcar `accessed = true`
   - Se não encontrado (cache miss):
     - Evocar entrada usando LRU/Clock
     - Ler setor do disco
     - Inserir no cache
     - Copiar dados para buffer

3. **`void cache_write(block_sector_t sector, const void *buffer)`**
   - Procurar setor no cache
   - Se encontrado: atualizar dados, marcar `dirty = true` e `accessed = true`
   - Se não encontrado:
     - Evocar entrada
     - Inserir no cache
     - Marcar `dirty = true`
   - **Write-back:** não escrever imediatamente no disco

4. **`void cache_flush(void)`**
   - Escrever todos os blocos dirty no disco
   - Chamar em `filesys_done()` para persistir ao desligar

5. **`block_sector_t cache_evict(void)`**
   - Implementar algoritmo Clock (Second-Chance LRU):
     - Percorrer cache circularmente
     - Se `accessed = true`: marcar `accessed = false` e continuar
     - Se `accessed = false`: evocar essa entrada
   - Se entrada evocada for dirty, escrever no disco primeiro
   - Retornar índice da entrada evocada

6. **`void cache_readahead(block_sector_t sector)`**
   - Pré-carregar setor de forma assíncrona
   - Não bloquear operação atual

### Integração com Block Device

**Opção A - Modificar `src/devices/block.c`:**
```c
void block_read(struct block *block, block_sector_t sector, void *buffer) {
    cache_read(sector, buffer);  // Usar cache em vez de disco direto
}

void block_write(struct block *block, block_sector_t sector, const void *buffer) {
    cache_write(sector, buffer);  // Usar cache
}
```

**Opção B - Criar wrappers no inode (Recomendada):**
- Manter `block_read/write` intactos
- Inode chama `cache_read/write` diretamente
- Mais controle e menos acoplamento

### Threads Assíncronas

**Write-Behind Thread:**
```c
static void write_behind_thread(void *aux UNUSED) {
    while (true) {
        timer_sleep(WRITE_BEHIND_INTERVAL);  // Ex: 5 segundos
        cache_flush();
    }
}
```

**Read-Ahead Thread:**
```c
static void readahead_thread(void *aux UNUSED) {
    while (true) {
        block_sector_t sector = readahead_queue_pop();  // Fila de pré-leitura
        if (sector != (block_sector_t)-1) {
            uint8_t buffer[BLOCK_SECTOR_SIZE];
            cache_read(sector, buffer);  // Carregar no cache
        }
    }
}
```

### Sincronização

**Desafios:**
- Múltiplos processos acessando mesmo bloco
- Eviction enquanto bloco está sendo lido/escrito
- Write-behind thread vs. operações de escrita

**Solução:**
1. **Lock global (`cache_lock`)**: proteger busca e eviction
2. **Lock por entrada (`entry_lock`)**: proteger leitura/escrita de dados
3. **Readers-writers pattern**: permitir múltiplos leitores ou um escritor

**Ordem de aquisição de locks (para evitar deadlock):**
1. `cache_lock` (encontrar/evocar entrada)
2. `entry_lock` (acessar dados da entrada)
3. Liberar `cache_lock` antes de I/O de disco

### Performance

**Métricas esperadas:**
- Hit rate > 80% em workloads sequenciais
- Redução significativa em chamadas `block_read/write`

### Testes Afetados

- `filesys/extended/syn-rw` - sincronização de leitura/escrita
- Todos os testes se beneficiam de performance melhorada

---

## Feature 4: File System Synchronization

### Objetivo

Garantir corretude em acessos concorrentes ao sistema de arquivos:
- Múltiplos processos lendo/escrevendo mesmo arquivo
- Extensão simultânea de arquivo
- Criação/remoção concorrente de arquivos

### Problemas a Resolver

1. **Race condition em extensão de arquivo:**
   - Processos A e B tentam estender arquivo simultaneamente
   - Solução: lock durante alocação de blocos

2. **Race condition em leitura vs escrita:**
   - Processo A lê enquanto B escreve
   - Requisito: A não pode ler dados corrompidos
   - Solução: sincronização no nível do inode

3. **Fairness:**
   - Leitores não devem bloquear escritores indefinidamente
   - Escritores não devem bloquear leitores indefinidamente

### Estratégias de Sincronização

**Opção A - Lock Global do Filesystem:**
- Um único lock para todas as operações
- **Vantagens:** simples, sem deadlocks
- **Desvantagens:** serializa tudo, baixo paralelismo

**Opção B - Locks Granulares (Recomendada):**
- Lock por inode
- Lock por diretório
- Lock do free_map
- **Vantagens:** alto paralelismo
- **Desvantagens:** mais complexo, risco de deadlock

### Implementação

**Adicionar em `struct inode`:**
```c
struct lock inode_lock;        // Protege extensão e remoção
int readers;                   // Número de leitores ativos
int writers;                   // Número de escritores ativos (0 ou 1)
struct condition can_read;     // Condição para leitores
struct condition can_write;    // Condição para escritores
```

**Funções de Sincronização:**

1. **`inode_lock_read(struct inode *inode)`**
   - Permitir múltiplos leitores
   - Bloquear se há escritor

2. **`inode_unlock_read(struct inode *inode)`**
   - Decrementar leitores
   - Sinalizar escritores se necessário

3. **`inode_lock_write(struct inode *inode)`**
   - Permitir apenas um escritor
   - Bloquear se há leitores ou escritor

4. **`inode_unlock_write(struct inode *inode)`**
   - Liberar escritor
   - Sinalizar próximo leitor/escritor

**Proteger Operações Críticas:**

1. **`inode_write_at()`** - extensão de arquivo:
```c
inode_lock_write(inode);
// Alocar novos blocos
// Atualizar length
inode_unlock_write(inode);
```

2. **`inode_read_at()`**:
```c
inode_lock_read(inode);
// Ler dados
inode_unlock_read(inode);
```

3. **`free_map_allocate()`**:
```c
lock_acquire(&free_map_lock);
// Encontrar e marcar setor livre
lock_release(&free_map_lock);
```

### Prevenir Deadlocks

**Regras de Ordenação:**
1. Nunca adquirir lock de inode enquanto segura lock de diretório
2. Ordem consistente ao adquirir múltiplos locks
3. Evitar locks aninhados quando possível

### Testes Afetados

- `filesys/extended/syn-rw`
- `filesys/extended/grow-two-files`
- Testes de persistence que criam múltiplos arquivos

---

## Ordem de Implementação Recomendada

### Opção A - Stanford Guide (Buffer Cache First)

1. **Buffer Cache** → isolado, não quebra código existente
2. **Indexed & Extensible Files** → modifica estrutura base
3. **Subdirectories** → adiciona funcionalidade final

**Vantagens:**
- Cache pode ser testado independentemente
- Performance melhorada desde o início

**Desvantagens:**
- Lógica complexa antes da base estrutural
- Pode precisar refatorar cache após mudar inode

### Opção B - Lógica Incremental (Recomendada)

1. **Indexed & Extensible Files** → base estrutural necessária
2. **Subdirectories** → funcionalidade independente do cache
3. **Buffer Cache** → otimização final sem modificar lógica

**Vantagens:**
- Constrói base sólida primeiro
- Subdirectories funcionam sem cache
- Cache é camada de otimização final

**Desvantagens:**
- Performance só melhora no final

### Decisão: Opção B

**Justificativa:**
- Indexed files são fundamentais para tudo
- Subdirectories testáveis independentemente
- Cache é transparente para resto do código

---

## Estratégia de Branches Git

Cada feature terá sua própria branch para desenvolvimento isolado:

1. `feat/indexed-files` - Feature 1
2. `feat/subdirectories` - Feature 2  
3. `feat/buffer-cache` - Feature 3
4. `feat/fs-sync` - Feature 4 (se necessário como branch separada)

**Workflow:**
1. Criar branch a partir de `main`
2. Implementar feature completa
3. Testar com `make check`
4. Atualizar `report-FS.md` com documentação
5. Merge para `main` após revisão

---

## Cronograma Estimado

| Feature                    | Complexidade | Tempo Estimado | Testes |
|----------------------------|--------------|----------------|--------|
| Indexed & Extensible Files | Alta         | 6-8 horas      | 21     |
| Subdirectories             | Alta         | 8-10 horas     | 24     |
| Buffer Cache               | Média        | 4-6 horas      | 1+     |
| File System Sync           | Média        | 2-3 horas      | N/A    |
| **Total**                  | -            | **20-27h**     | **46** |

---

## Próximos Passos

1. ✅ Revisar e aprovar plano de implementação
2. ⏳ Escolher primeira feature a implementar
3. ⏳ Criar branch apropriada
4. ⏳ Implementar feature escolhida
5. ⏳ Testar e documentar
6. ⏳ Repetir para próximas features

---