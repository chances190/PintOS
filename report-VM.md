# PintOS - Relatório de Implementação

## Projeto 3 - Memória Virtual

### Parte 1 - Stack Growth
**Objetivo**: Criar infraestrutura básica (Frame Table + Supplemental Page Table + Stack Growth)


#### `vm/frame.h`, `vm/frame.c`
- Criada estrutura `struct frame_table_entry` com campos `frame`, `owner`, `spte`, `elem` para rastrear frames físicos alocados.
- Declarado lock global `frame_table_lock` para proteger acesso concorrente à frame table.
- Implementada `frame_table_init()` para inicializar lista global e lock durante boot.
- Implementada `frame_alloc()` para alocar frames com rastreamento automático na frame table.
- Implementada `frame_free()` para liberar frames e remover da frame table.

#### `vm/page.h`, `vm/page.c`
- Criada estrutura `struct sup_page_table_entry` com campos para endereço virtual, timestamps, flags de estado, informações de swap e arquivo para armazenar metadados de páginas.
- Implementada `spt_init()` para inicializar SPT vazio em cada thread.
- Implementada `spt_insert()` para adicionar entradas ao SPT com verificação de duplicatas.
- Implementada `spt_lookup()` para buscar entrada por endereço virtual normalizado.
- Implementada `spt_delete()` para remover e liberar entrada do SPT.
- Implementada `spt_destroy()` para cleanup completo do SPT em process exit.
- Implementada `spt_update_access_time()` para atualizar timestamp de acesso para algoritmos de eviction.

#### `threads/thread.h`
- Adicionado campo `struct list sup_page_table` em `struct thread` para manter SPT por processo.

#### `threads/thread.c`
- Incluído `vm/page.h`.
- Modificado `init_thread()` para chamar `spt_init()` e garantir SPT inicializado em cada thread.

#### `threads/init.c`
- Incluído `vm/frame.h`.
- Modificado `main()` para chamar `frame_table_init()` durante boot do sistema.

#### `userprog/process.c`
- Incluído `vm/page.h`.
- Modificado `process_exit()` para chamar `spt_destroy()` e liberar metadados antes de destruir page directory.

#### `userprog/exception.c`
- Incluídos headers necessários para alocação e manipulação de páginas.
- Definidas constantes `USER_STACK_BASE` e `STACK_HEURISTIC` para limites de validação.
- Implementada `install_page()` para centralizar instalação de páginas com verificação de duplicatas.
- Modificado `page_fault()` para implementar stack growth automático com validação de endereços e alocação sob demanda.

#### Design

**1. Frame Table - Rastreamento de Frames Físicos**

A Frame Table é uma lista global (`frame_table`) que rastreia todos os frames físicos alocados. Cada entrada (`frame_table_entry`) contém: ponteiro ao frame físico, thread proprietária, e link para a entrada SPT correspondente. Um lock global (`frame_table_lock`) serializa todos os acessos.

A função `frame_alloc()` funciona como wrapper sobre `palloc_get_page()`: aloca o frame, cria uma `frame_table_entry`, insere na lista global, e retorna o ponteiro. A função `frame_free()` faz o inverso: remove da lista, libera a estrutura, chama `palloc_free_page()`. Essa centralização permite que, ao precisar escolher vítima para eviction, o kernel percorra `frame_table` e examine todos os frames alocados.

A decisão de usar lista simples (ao invés de hash table) foi tomada porque eviction percorre todos os frames sequencialmente (algoritmo Clock), então lookup O(1) por endereço não é necessário. A busca individual de frame é rara — só ocorre durante eviction, que de qualquer forma itera sobre todos.

**2. Supplemental Page Table (SPT) - Metadados de Páginas**

Cada thread mantém sua própria lista `sup_page_table` de entradas `sup_page_table_entry`. O SPT armazena metadados de todas as páginas do processo, incluindo páginas que não estão presentes fisicamente. Cada entrada registra:
- Endereço virtual da página (`user_vaddr`)
- Timestamp de acesso (`access_time`)
- Flags: `dirty`, `accessed`, `writable`, `is_swapped`, `is_mmap`
- Informações de swap: `swap_index`
- Informações de arquivo: `file`, `offset`, `read_bytes`, `zero_bytes`

A função `spt_lookup()` normaliza endereços com `pg_round_down()` antes de buscar, permitindo consultar por qualquer endereço dentro da página. O SPT persiste durante toda a vida do processo — quando página é evicted, a entrada permanece com flags atualizados (`is_swapped=true`, `swap_index` preenchido), permitindo reclamação posterior.

A escolha por lista simples (ao invés de hash table) se justifica porque processos no PintOS têm poucas páginas (< 100 tipicamente), então busca linear O(n) é aceitável. A simplicidade facilita depuração e reduz erros, embora em processos maiores possa se tornar gargalo (substituível por hash mantendo interface).

Os campos `file`, `offset`, `read_bytes`, `zero_bytes` foram incluídos desde o início porque serão usados para lazy loading de executáveis e mmap. Os campos `is_swapped` e `swap_index` suportam swap. Incluir todos agora evita quebrar interface depois, ao custo de ~48 bytes por entrada.

**3. Stack Growth Automático**

Quando ocorre page fault em endereço não mapeado, `page_fault()` determina se é stack growth legítimo ou erro. A validação ocorre em etapas:

1. Verificações de segurança:
   - `fault_addr == NULL` → inválido (NULL pointer)
   - `fault_addr >= PHYS_BASE` → inválido (acesso a kernel)
   - `fault_addr < USER_STACK_BASE (0x08048000)` → inválido (abaixo de memória de usuário)
   - `fault_addr < ESP - 32` → inválido (não é stack legítimo)

2. Consulta SPT: `spt_lookup()` verifica se página já existe (lazy load ou swapped). Se não existe, pode ser stack growth.

3. Alocação: Se todas validações passam e SPT não contém a página:
   - `palloc_get_page(PAL_USER | PAL_ZERO)` aloca frame zerado
   - `install_page(upage, frame, writable=true)` instala mapeamento
   - `spt_insert()` cria entrada SPT para rastrear
   - Processo continua transparentemente

O limite de 32 bytes abaixo de ESP (ao invés de 4KB) foi escolhido para suportar PUSHA (instrução x86 que acessa 32 bytes atomicamente: 8 registradores × 4 bytes), mas rejeitar acessos muito distantes que provavelmente são bugs. Permitir 4KB inteiros mascararia erros de ponteiros, enquanto 32 bytes protege contra bugs comuns mas pode rejeitar padrões raros como alocação de array gigante na stack.

**4. Separação de Responsabilidades**

Três estruturas gerenciam memória com propósitos distintos:

- **Page Directory**: Hardware x86 (MMU). Mapeia virtual→físico. Conhece apenas páginas presentes. Operações: `pagedir_set_page()`, `pagedir_clear_page()`.

- **Frame Table**: Software do kernel, global. Rastreia frames físicos: quem usa, qual página mapeia. Necessária para escolher vítima na eviction e liberar frames em exit.

- **SPT**: Software do kernel, per-process. Conhece TODAS as páginas (presentes, swapped, lazy, mmap). Armazena metadados que Page Directory não mantém.

Instalação de página:
1. `palloc_get_page()` → aloca frame
2. `frame_alloc()` → registra ownership na Frame Table
3. `pagedir_set_page()` → cria mapeamento virtual→físico
4. `spt_insert()` → registra metadados

Eviction:
1. Percorrer Frame Table (Clock) para escolher vítima
2. Consultar SPT da vítima para metadados
3. Escrever no swap se dirty
4. Atualizar SPT: `is_swapped=true`, `swap_index` preenchido
5. `pagedir_clear_page()` remove mapeamento
6. `frame_free()` libera frame
7. Entrada SPT permanece para reclamação

Separar as três estruturas (ao invés de unificar) foi necessário porque Page Directory é imposta pelo hardware, Frame Table é global (compartilhada entre processos) e SPT é per-process. A separação facilita raciocínio sobre ownership.

**5. Integração com Syscalls**

Stack growth é transparente para syscalls existentes. Quando `read(fd, buffer, size)` recebe `buffer` em página não alocada:

1. Syscall tenta `put_user()`/`memcpy()`
2. Page fault dispara
3. `page_fault()` valida `buffer >= ESP - 32`
4. Aloca página automaticamente
5. Instrução reexecuta
6. `put_user()` sucede
7. Syscall completa

Nenhum syscall do Projeto 2 foi modificado. A validação existente (`is_user_vaddr()`, `pagedir_get_page()`) funciona — se página não está presente, page fault aloca antes do acesso. O overhead de fault é desprezível (página alocada uma vez, hits de TLB depois).



#### Resultados de Testes
- ✅ `vm/pt-grow-stack` - Stack growth básico alocando objeto de 4KB.
- ✅ `vm/pt-grow-stk-sc` - Stack cresce dentro de syscall ao acessar buffer não alocado.
- ✅ `vm/pt-grow-pusha` - Suporte à instrução PUSHA que acessa 32 bytes abaixo de ESP.
- ✅ `vm/pt-big-stk-obj` - Alocação de objeto grande (64KB) na stack.
- ✅ `vm/pt-bad-addr` - Rejeição de acesso a endereço inválido fora de user space.
- ✅ `vm/pt-bad-read` - Rejeição de read() para endereço abaixo do stack pointer.
- ✅ `vm/pt-write-code` - Rejeição de escrita direta em segmento de código.
- ✅ `vm/pt-write-code2` - Rejeição de read() para segmento de código via syscall.
- ✅ `vm/pt-grow-bad` - Rejeição de acesso muito abaixo de ESP (4096 bytes).


---

### Parte 2 - Swap e Lazy Loading
**Objetivo**: Implementar lazy loading de executáveis, swap space, e eviction com algoritmo Clock

#### `src/vm/swap.c`, `src/vm/swap.h`
- Criado arquivo `swap.c` com implementação completa do sistema de swap.
- Criado arquivo `swap.h` com interface pública das funções de swap.
- Implementada `swap_init()` para inicializar swap table usando block device `BLOCK_SWAP`.
- Criada estrutura `swap_table` (bitmap) para rastrear slots livres/ocupados no swap disk.
- Implementada `swap_out()` para escrever uma página para o swap disk e retornar o slot index.
- Implementada `swap_in()` para ler uma página do swap disk de volta à memória e liberar o slot.
- Implementada `swap_free()` para liberar um slot sem ler a página (usada ao desalocar página swapped).
- Definida constante `SECTORS_PER_PAGE` (8 setores de 512 bytes = 4096 bytes = 1 página).
- Adicionado lock `swap_lock` para proteger operações concorrentes no swap table.

#### `src/vm/vm.c`, `src/vm/vm.h`
- Criado arquivo `vm.c` como camada de abstração que coordena frame, page e swap.
- Criado arquivo `vm.h` com interface pública das funções de VM.
- Implementada `vm_load()` para carregar página sob demanda a partir de arquivo ou swap.
- Implementada `vm_palloc()` para alocar frame físico e criar entrada SPT para página anônima.
- Implementada `vm_free()` para liberar frame físico e remover do SPT, liberando swap slot se necessário.
- Implementada `vm_mmap()` para criar região de memória mapeada e popular SPT com entradas lazy.
- Implementada `vm_munmap()` para desalocar região mapeada, fazer write-back de páginas dirty e liberar recursos.

#### `src/vm/frame.c`, `src/vm/frame.h`
- Refatorada estrutura `struct frame_table_entry` com campos `kpage`, `spte`, `pagedir`, `pinned`.
- Modificada `frame_alloc()` para aceitar `spte` como argumento e implementar eviction quando `palloc` falha.
- Implementada `frame_table_evict_page()` com algoritmo Clock (Second-Chance) para escolher vítima.
- Adicionada variável estática `clock_hand` para rastrear posição atual no algoritmo Clock.
- Implementado pinning de frames: `frame_pin()` e `frame_unpin()` para prevenir eviction de frames críticos.
- Modificada `frame_free()` para limpar mapeamento via `pagedir_clear_page()` antes de liberar frame.
- Implementada `frame_lookup_spte()` para buscar entrada SPT associada a um kernel page.
- Lógica de eviction: skip frames pinned, clear accessed bit (second chance), evict quando accessed=0.
- Eviction de páginas file-backed: write-back apenas se dirty e writable, caso contrário apenas evict.
- Eviction de páginas anônimas: sempre swap out para disco.

#### `src/vm/page.c`, `src/vm/page.h`
- Modificada estrutura `struct sup_page_table_entry` com campos `upage`, `type`, `swap_slot`, `file`, `file_offset`, `read_bytes`, `zero_bytes`, `file_writable`.
- Criado enum `page_type` com valores `PAGE_FILE`, `PAGE_ANON`, `PAGE_SWAP` para indicar origem da página.
- Adicionada estrutura `struct mmap_region` com campos `mapid`, `start`, `page_cnt`, `file`, `elem` para rastrear mapeamentos.
- Removidas funções `spt_insert()`, `spt_delete()`, `spt_update_access_time()` não utilizadas.
- Modificada `spt_lookup()` para usar campo `upage` ao invés de `user_vaddr`.
- Implementada `spt_create_page_file()` para criar entrada SPT de página file-backed (segmentos executáveis ou mmap).
- Implementada `spt_create_page_anon()` para criar entrada SPT de página anônima (stack, zero pages).
- Modificada `spt_destroy()` para simplificar limpeza (apenas libera entradas SPT, não toca em frames).

#### `src/threads/thread.h`, `src/threads/thread.c`
- Adicionado campo `void *user_esp` em `struct thread` para salvar stack pointer de usuário durante syscalls.
- Adicionado campo `struct list mappings` em `struct thread` para lista de regiões mapeadas.
- Adicionado campo `int next_mapid` em `struct thread` para próximo ID de mapeamento a alocar.
- Modificado `init_thread()` para inicializar `mappings` vazia e `next_mapid = 1`.

#### `src/threads/init.c`
- Incluído header `vm/swap.h` (condicional `#ifdef VM`).
- Adicionada chamada a `swap_init()` em `main()` (após `ide_init()` e condicionalmente para `USERPROG && VM`).

#### `src/userprog/process.c`
- Incluído header `vm/vm.h`.
- Modificada `load_segment()` para lazy loading: ao invés de ler arquivo imediatamente, cria entradas SPT com `spt_create_page_file()`.
- Modificada `setup_stack()` para usar `vm_palloc()` ao invés de `palloc_get_page()` + `pagedir_install_page()`.
- Modificado `process_exit()` para iterar sobre `mappings` e chamar `vm_munmap()` para cada região antes de destruir SPT.
- Removida lógica de `file_read()` e `memset()` de `load_segment()`, substituída por criação de entradas SPT apenas.

#### `src/userprog/exception.c`
- Incluídos headers `filesys/file.h`, `vm/frame.h`, `vm/swap.h`, `vm/vm.h`.
- Modificado `page_fault()` para carregar páginas sob demanda de arquivo ou swap.
- Adicionada validação: protection violation (not_present=false) é fatal.
- Adicionada validação: endereços de usuário inválidos (NULL, kernel) são fatais.
- Modificada lógica de stack growth: agora cria entrada SPT com `spt_create_page_anon()` e aloca frame com `vm_palloc()`.
- Adicionada lógica: se página existe em SPT, carrega com `vm_load()` (file-backed ou swap).
- Adicionada lógica de recuperação para kernel acessando user memory: define `eip = eax` e `eax = 0xffffffff`.

#### `src/userprog/syscall.c`
- Incluídos headers `vm/page.h` e `vm/vm.h`.
- Modificado `syscall_handler()` para salvar `user_esp` em `thread_current()->user_esp` no início.
- Implementada `syscall_mmap()` para delegar a `vm_mmap()`.
- Implementada `syscall_munmap()` para delegar a `vm_munmap()`.
- Removida mensagem "not yet implemented" das syscalls mmap/munmap.

#### `src/userprog/pagedir.c`, `src/userprog/pagedir.h`
- Renomeada função `pagedir_install_page()` para `pagedir_map_page()` para consistência de nomenclatura.
- Adicionada função `pagedir_get_page()` (anteriormente estática `get_page()`) para expor no header.
- Adicionados debug prints em `pagedir_map_page()`, `pagedir_clear_page()`, `pagedir_get_page()`.
- Modificado `set_page()` de volta para estático.

#### `src/Makefile.build`
- Adicionadas linhas `vm_SRC += vm/swap.c` e `vm_SRC += vm/vm.c` para incluir novos arquivos no build.

#### `src/lib/debug.h`
- Modificada macro `DEBUG_PRINT`: trocada condição de `#if DEBUG` para `#ifdef DEBUG` para permitir compilação mesmo quando DEBUG não está definido.

#### `.github/copilot-instructions.md`
- Adicionada instrução alternativa para executar testes individuais com `make tests/<phase>/<test>.result`.

#### Design

**1. Lazy Loading de Executáveis**

Ao carregar um executável, `load_segment()` agora registra metadados no SPT ao invés de carregar imediatamente. Para cada página do segmento:

1. Cria `sup_page_table_entry` com `spt_create_page_file()`:
   - `upage`: endereço virtual da página
   - `type = PAGE_FILE`: indica origem em arquivo
   - `file`, `file_offset`: localização no disco
   - `read_bytes`, `zero_bytes`: quantos bytes ler/zerar
   - `file_writable`: permissões da página

2. Insere entrada no SPT da thread (não aloca frame ainda)

3. Quando processo tenta acessar a página, page fault dispara

4. `page_fault()` chama `vm_load()`:
   - Aloca frame com `frame_alloc()`
   - Lê dados do arquivo com `file_read_at()`
   - Zera bytes restantes com `memset()`
   - Mapeia no page directory com `pagedir_map_page()`

**Vantagens desta abordagem:**

- **Redução de latência inicial**: processo inicia mais rápido pois não espera carregar todo executável
- **Economia de memória**: páginas nunca acessadas nunca são carregadas (ex: código de erro raro)
- **Compartilhamento implícito**: múltiplas instâncias do mesmo executável podem compartilhar páginas read-only (não implementado no PintOS, mas arquitetura permite)

**Trade-offs:**

- **Primeiro acesso é lento**: page fault + disk I/O adiciona ~10ms por página na primeira vez
- **Fragmentação de I/O**: ao invés de uma leitura sequencial grande, muitas leituras pequenas aleatórias
- **Complexidade**: requer coordenação entre SPT, frame table, swap, page fault handler

Esta decisão foi tomada porque PintOS é um sistema educacional onde a maioria dos programas são pequenos (< 10 páginas de código), então o overhead de page fault é aceitável. Em sistema de produção, pode-se fazer prefetching de próximas páginas sequenciais para mitigar latência.

**2. Swap Space**

O swap permite que sistema continue operando quando memória física está cheia, movendo páginas para disco temporariamente.

**Estrutura:**

- `swap_block`: block device obtido via `block_get_role(BLOCK_SWAP)`
- `swap_table`: bitmap rastreando slots livres/ocupados (1 slot = 1 página = 8 setores)
- `swap_lock`: lock global para proteger operações concorrentes

**Operações:**

- `swap_out(kpage)`: encontra slot livre, escreve 8 setores para disco, marca slot como ocupado, retorna índice
- `swap_in(index, kpage)`: lê 8 setores do disco para memória, marca slot como livre
- `swap_free(index)`: marca slot como livre sem ler (usado ao desalocar página swapped)

**Integração com eviction:**

Quando `frame_alloc()` falha (sem memória livre):

1. Chama `frame_table_evict_page()` que escolhe vítima com Clock
2. Se vítima é página anônima (stack, heap):
   - Chama `swap_out(kpage)` para mover para swap
   - Atualiza SPT: `type = PAGE_SWAP`, `swap_slot = index`
   - Limpa mapeamento com `pagedir_clear_page()`
3. Se vítima é página file-backed:
   - Se dirty e writable: `file_write_at()` para flush mudanças
   - Limpa mapeamento (pode recarregar do arquivo depois)
   - SPT mantém `type = PAGE_FILE`
4. Frame é reutilizado para nova página

**Quando página swapped é acessada:**

1. Page fault dispara
2. `page_fault()` consulta SPT, encontra `type = PAGE_SWAP`
3. Chama `vm_load()` que chama `swap_in(swap_slot, kpage)`
4. Atualiza SPT: `type = PAGE_ANON`
5. Mapeia no page directory

**3. Algoritmo de Eviction - Clock (Second-Chance)**

Implementado em `frame_table_evict_page()`, baseado no algoritmo Clock clássico que aproxima LRU:

**Estrutura:**

- `frame_table`: lista circular de frames alocados
- `clock_hand`: ponteiro para próximo frame a examinar

**Algoritmo:**

1. Começa em `clock_hand` (última posição examinada)
2. Para cada frame na lista circular:
   - Se `pinned = true`: skip (frame não pode ser evicted)
   - Consulta accessed bit no page directory
   - Se `accessed = 1`: clear bit (segunda chance), avança
   - Se `accessed = 0`: escolhe como vítima, break
3. Se nenhuma vítima encontrada em 2 voltas completas: retorna NULL (todos pinned)
4. Atualiza `clock_hand` para próxima posição

**Pinning de frames:**

Frames podem ser marcados como pinned para prevenir eviction durante operações críticas:

- Durante page fault enquanto carrega dados do disco
- Durante syscalls que acessam user buffers
- Durante operações de I/O assíncronas

Sem pinning, frame poderia ser evicted enquanto ainda está sendo usado, causando corrupção ou race condition.

**Por que Clock ao invés de LRU?**

- **LRU perfeito**: requer atualizar timestamp em cada acesso → overhead proibitivo
- **Clock**: hardware atualiza accessed bit automaticamente em cada acesso → zero overhead
- **Aproximação boa**: Clock se comporta similarmente a LRU na prática (páginas acessadas recentemente têm accessed=1)
- **Simplicidade**: implementação simples com lista circular + ponteiro

**4. Coordenação Frame-Page-Swap**

A interação entre os três subsistemas segue uma hierarquia clara:

**vm_load() - Carregar página sob demanda:**

```
1. Consulta SPT por upage
2. Aloca frame com frame_alloc(spte)
   └─> Se palloc falha, frame_alloc chama eviction
       └─> Eviction escreve para swap ou arquivo
3. Carrega dados:
   - Se PAGE_SWAP: swap_in()
   - Se PAGE_FILE: file_read_at()
4. Mapeia com pagedir_map_page()
5. Unpin frame
```

**vm_palloc() - Alocar nova página anônima:**

```
1. Cria entrada SPT com spt_create_page_anon()
2. Aloca frame com frame_alloc(spte)
   └─> Frame retorna zerado e pinned
3. Insere SPT entry na lista do thread
4. Mapeia com pagedir_map_page()
5. Unpin frame
```

**vm_free() - Liberar página:**

```
1. Busca spte via frame_lookup_spte(kpage)
2. Se PAGE_SWAP: swap_free(swap_slot)
3. Chama frame_free(kpage)
   └─> Remove do frame_table
   └─> pagedir_clear_page()
   └─> palloc_free_page()
4. Remove spte do SPT
```

**Invariantes mantidos:**

- Todo frame na frame_table tem spte válido (ou NULL temporariamente durante eviction)
- Todo spte na lista tem type válido (FILE/ANON/SWAP)
- Páginas swapped têm mapeamento cleared no page directory mas SPT entry persiste
- Frames pinned nunca são escolhidos como vítimas

**5. Memory Mapping (mmap/munmap)**

Implementado através das funções `vm_mmap()` e `vm_munmap()`, permitindo mapear arquivos diretamente na memória do processo.

**vm_mmap() - Criar mapeamento:**

Validações:
- `addr` deve ser não-NULL, user space, alinhado a página
- `fd` deve ser ≥ 2 (não stdin/stdout)
- Arquivo deve ter tamanho > 0
- Região `[addr, addr + file_length)` não pode sobrepor páginas existentes no SPT

Operação:
1. Reabre arquivo com `file_reopen()` (handle independente)
2. Calcula `page_cnt = (file_length + PGSIZE - 1) / PGSIZE`
3. Cria `mmap_region` com `mapid` único
4. Para cada página:
   - Cria `sup_page_table_entry` com `spt_create_page_file()`
   - Define `type = PAGE_FILE`, `file_writable = true`
   - Insere no SPT (páginas NÃO são carregadas ainda)
5. Adiciona `mmap_region` à lista `mappings` do thread

**vm_munmap() - Desalocar mapeamento:**

1. Busca `mmap_region` por `mapid`
2. Para cada página da região:
   - Consulta SPT para obter metadados
   - Se página está presente em memória:
     - Se dirty e writable: write-back com `file_write_at()`
     - Limpa mapeamento com `pagedir_clear_page()`
     - Libera frame com `frame_free()`
   - Se página está no swap: `swap_free(swap_slot)`
   - Remove entrada SPT
3. Fecha arquivo com `file_close()`
4. Remove e libera `mmap_region`

**Ciclo de vida completo:**

```
mmap(fd, addr)
  └─> Cria SPT entries, páginas lazy
  
Primeiro acesso
  └─> Page fault → vm_load() → file_read_at()
  
Escrita na página
  └─> Hardware marca dirty bit
  
Eviction (se memória cheia)
  └─> frame_evict() → file_write_at() se dirty
  └─> SPT mantém type=FILE para recarregar depois
  
munmap()
  └─> Write-back de páginas dirty
  └─> Libera frames e SPT entries
  └─> Fecha arquivo
```

**Diferença entre mmap e segmentos executáveis:**

Ambos usam `PAGE_FILE`, mas:
- Segmentos executáveis: `file_writable = false` (read-only, nunca write-back)
- Mmap: `file_writable = true` (read-write, write-back se dirty)

**Tratamento de edge cases:**

- **mmap de stdin/stdout**: rejeitado (fd < 2)
- **mmap em NULL**: rejeitado
- **mmap desalinhado**: rejeitado (addr não múltiplo de PGSIZE)
- **mmap sobre código/dados/stack**: rejeitado (sobreposição detectada via SPT lookup)
- **mmap após fork**: mappings NÃO são herdados (lista `mappings` é per-thread, não copiada)
- **arquivo deletado**: mapeamento persiste (file handle ainda válido)

#### Resultados de Testes
- ✅ `vm/page-linear` - Lazy loading com acesso linear a 2MB de memória.
- ✅ `vm/page-parallel` - Lazy loading com 4 processos concorrentes.
- ✅ `vm/page-merge-seq` - Lazy loading com ordenação sequencial de chunks.
- ✅ `vm/page-merge-par` - Lazy loading com ordenação paralela de chunks.
- ✅ `vm/page-merge-stk` - Lazy loading com uso intensivo de stack (qsort).
- ✅ `vm/page-merge-mm` - Lazy loading com memory-mapped I/O.
- ✅ `vm/page-shuffle` - Lazy loading com acesso aleatório embaralhando 128KB.

---

### Parte 3 - Memory Mapping
**Objetivo**: Implementar syscalls mmap/munmap com lazy loading e write-back, incluindo todas as validações

_Nota: A implementação de mmap/munmap foi integrada na Parte 2, pois compartilha a mesma infraestrutura de lazy loading. Veja documentação em `src/vm/vm.c` (funções `vm_mmap()` e `vm_munmap()`) e design detalhado na seção anterior._

#### Resultados de Testes

**Operações Básicas:**
- ✅ `vm/mmap-read` - Leitura de arquivo via memory mapping.
- ✅ `vm/mmap-write` - Escrita em arquivo via memory mapping.
- ✅ `vm/mmap-close` - Mapping persiste após fechar file descriptor.
- ✅ `vm/mmap-unmap` - Região torna-se inacessível após munmap.
- ✅ `vm/mmap-exit` - Write-back automático de páginas dirty no exit.
- ✅ `vm/mmap-clean` - Páginas não modificadas não são escritas em munmap.
- ✅ `vm/mmap-remove` - Mapping persiste mesmo após deletar arquivo.
- ✅ `vm/mmap-shuffle` - Acesso aleatório via memory mapping embaralhando 128KB.

**Validação de Argumentos:**
- ✅ `vm/mmap-bad-fd` - Rejeição de mmap com file descriptor inválido.
- ✅ `vm/mmap-null` - Rejeição de mmap em endereço NULL.
- ✅ `vm/mmap-zero` - Tratamento de mmap de arquivo vazio (length = 0).
- ✅ `vm/mmap-misalign` - Rejeição de mmap em endereço não alinhado a página.

**Detecção de Overlaps:**
- ✅ `vm/mmap-twice` - Múltiplos mappings do mesmo arquivo em endereços diferentes.
- ✅ `vm/mmap-overlap` - Rejeição de mappings sobrepostos.
- ✅ `vm/mmap-over-code` - Rejeição de mmap sobre segmento de código.
- ✅ `vm/mmap-over-data` - Rejeição de mmap sobre segmento de dados.
- ✅ `vm/mmap-over-stk` - Rejeição de mmap sobre stack.

**Edge Cases:**
- ✅ `vm/mmap-inherit` - Mappings não são herdados por processos filhos.
