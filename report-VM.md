# PintOS - Relatório de Implementação

## Projeto 3 - Memória Virtual

---

### 🌿 Branch 1: feat/stack-growth
**Objetivo**: Criar infraestrutura básica (Frame Table + Supplemental Page Table + Stack Growth)

#### Arquivos Modificados

##### `vm/frame.h`, `vm/frame.c`
- Criada estrutura `struct frame_table_entry` com campos `frame`, `owner`, `spte`, `elem` para rastrear frames físicos alocados.
- Declarado lock global `frame_table_lock` para proteger acesso concorrente à frame table.
- Implementada `frame_table_init()` para inicializar lista global e lock durante boot.
- Implementada `frame_alloc()` para alocar frames com rastreamento automático na frame table.
- Implementada `frame_free()` para liberar frames e remover da frame table.

##### `vm/page.h`, `vm/page.c`
- Criada estrutura `struct sup_page_table_entry` com campos para endereço virtual, timestamps, flags de estado, informações de swap e arquivo para armazenar metadados de páginas.
- Implementada `spt_init()` para inicializar SPT vazio em cada thread.
- Implementada `spt_insert()` para adicionar entradas ao SPT com verificação de duplicatas.
- Implementada `spt_lookup()` para buscar entrada por endereço virtual normalizado.
- Implementada `spt_delete()` para remover e liberar entrada do SPT.
- Implementada `spt_destroy()` para cleanup completo do SPT em process exit.
- Implementada `spt_update_access_time()` para atualizar timestamp de acesso para algoritmos de eviction.

##### `threads/thread.h`
- Adicionado campo `struct list sup_page_table` em `struct thread` para manter SPT por processo.

##### `threads/thread.c`
- Incluído `vm/page.h`.
- Modificado `init_thread()` para chamar `spt_init()` e garantir SPT inicializado em cada thread.

##### `threads/init.c`
- Incluído `vm/frame.h`.
- Modificado `main()` para chamar `frame_table_init()` durante boot do sistema.

##### `userprog/process.c`
- Incluído `vm/page.h`.
- Modificado `process_exit()` para chamar `spt_destroy()` e liberar metadados antes de destruir page directory.

##### `userprog/exception.c`
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

### 🌿 Branch 2: feat/lazy-loading-swap
**Objetivo**: Implementar lazy loading de executáveis, swap space, e eviction com algoritmo Clock

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever:
1. Modificação de load_segment() para criar entradas SPT ao invés de carregar imediatamente
2. Implementação de swap.{c,h} com block device e swap table
3. Algoritmo Clock de eviction na Frame Table
4. Extensão de page_fault() para carregar de arquivo ou swap conforme SPT
5. Integração completa: lazy load → eviction → swap in/out
-->

#### Resultados de Testes
- ❌ `vm/page-linear` - Lazy loading com acesso linear a 2MB de memória.
- ❌ `vm/page-parallel` - Lazy loading com 4 processos concorrentes.
- ❌ `vm/page-merge-seq` - Lazy loading com ordenação sequencial de chunks.
- ❌ `vm/page-merge-par` - Lazy loading com ordenação paralela de chunks.
- ❌ `vm/page-merge-stk` - Lazy loading com uso intensivo de stack (qsort).
- ❌ `vm/page-merge-mm` - Lazy loading com memory-mapped I/O.
- ❌ `vm/page-shuffle` - Lazy loading com acesso aleatório embaralhando 128KB.

---

### 🌿 Branch 3: feat/mmap
**Objetivo**: Implementar syscalls mmap/munmap com lazy loading e write-back, incluindo todas as validações

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever:
1. Implementação de SYS_MMAP e SYS_MUNMAP em syscall.c
2. Criação de entradas SPT com flag is_mmap
3. Lazy loading de páginas mapeadas via page_fault()
4. Write-back de páginas dirty em munmap e process_exit
5. Validações: bad-fd, null, zero, misalign, overlaps, over-code/data/stk
6. Edge case: mappings não são herdados por filhos
-->

#### Resultados de Testes

**Operações Básicas:**
- ❌ `vm/mmap-read` - Leitura de arquivo via memory mapping.
- ❌ `vm/mmap-write` - Escrita em arquivo via memory mapping.
- ❌ `vm/mmap-close` - Mapping persiste após fechar file descriptor.
- ❌ `vm/mmap-unmap` - Região torna-se inacessível após munmap.
- ❌ `vm/mmap-exit` - Write-back automático de páginas dirty no exit.
- ❌ `vm/mmap-clean` - Páginas não modificadas não são escritas em munmap.
- ❌ `vm/mmap-remove` - Mapping persiste mesmo após deletar arquivo.
- ❌ `vm/mmap-shuffle` - Acesso aleatório via memory mapping embaralhando 128KB.

**Validação de Argumentos:**
- ❌ `vm/mmap-bad-fd` - Rejeição de mmap com file descriptor inválido.
- ❌ `vm/mmap-null` - Rejeição de mmap em endereço NULL.
- ❌ `vm/mmap-zero` - Tratamento de mmap de arquivo vazio (length = 0).
- ❌ `vm/mmap-misalign` - Rejeição de mmap em endereço não alinhado a página.

**Detecção de Overlaps:**
- ❌ `vm/mmap-twice` - Múltiplos mappings do mesmo arquivo em endereços diferentes.
- ❌ `vm/mmap-overlap` - Rejeição de mappings sobrepostos.
- ❌ `vm/mmap-over-code` - Rejeição de mmap sobre segmento de código.
- ❌ `vm/mmap-over-data` - Rejeição de mmap sobre segmento de dados.
- ❌ `vm/mmap-over-stk` - Rejeição de mmap sobre stack.

**Edge Cases:**
- ❌ `vm/mmap-inherit` - Mappings não são herdados por processos filhos.
