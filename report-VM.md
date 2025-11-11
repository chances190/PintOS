# PintOS - Relatório de Implementação

## Projeto 3 - Memória Virtual

---

### 🌿 Branch 1: vm-stack-growth
**Objetivo**: Criar infraestrutura básica (Frame Table + Supplemental Page Table + Stack Growth)

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever arquitetura do Frame Table, SPT e lógica de stack growth -->

#### Resultados de Testes
- ❌ `vm/pt-grow-stack` - Stack growth básico
- ❌ `vm/pt-grow-stk-sc` - Stack growth via syscall
- ❌ `vm/pt-grow-pusha` - Instrução PUSHA
- ❌ `vm/pt-big-stk-obj` - Objeto grande na stack
- ✅ `vm/pt-bad-addr` - Validação de endereços inválidos
- ✅ `vm/pt-bad-read` - Leitura de endereço inválido
- ✅ `vm/pt-write-code` - Tentativa de escrever em código
- ✅ `vm/pt-write-code2` - Tentativa de escrever em código (variação)
- ✅ `vm/pt-grow-bad` - Stack growth inválido

---

### 🌿 Branch 2: vm-paging
**Objetivo**: Implementar lazy loading de executáveis e paging sob demanda

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever como load_segment foi modificado e como page fault handler carrega páginas sob demanda -->

#### Resultados de Testes
- ❌ `vm/page-linear` - Acesso linear a páginas
- ❌ `vm/page-parallel` - Acesso paralelo (múltiplas threads)
- ❌ `vm/page-merge-seq` - Merge sequencial
- ❌ `vm/page-merge-par` - Merge paralelo
- ❌ `vm/page-merge-stk` - Merge com stack
- ❌ `vm/page-merge-mm` - Merge com memory mapping
- ❌ `vm/page-shuffle` - Acesso aleatório a páginas

---

### 🌿 Branch 3: vm-swap
**Objetivo**: Implementar swap space, eviction (Clock algorithm) e reclamação de páginas

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever Swap Table, algoritmo Clock de eviction, e processo de swap in/out -->

#### Resultados de Testes
- ❌ `vm/page-linear` - Agora com eviction (reteste)
- ❌ `vm/page-parallel` - Agora com eviction (reteste)
- ❌ `vm/page-shuffle` - Agora com eviction (reteste)
- _Nota: Testes de paging da Branch 2 devem passar completamente aqui com eviction_

---

### 🌿 Branch 4: vm-mmap-basic
**Objetivo**: Implementar syscalls mmap/munmap básicas e lazy loading de arquivos mapeados

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever syscalls mmap/munmap, lazy loading de MMAP, e write-back -->

#### Resultados de Testes
- ❌ `vm/mmap-read` - Leitura de arquivo mapeado
- ❌ `vm/mmap-write` - Escrita em arquivo mapeado
- ❌ `vm/mmap-close` - Fechar FD após mmap
- ❌ `vm/mmap-unmap` - Unmapping básico
- ❌ `vm/mmap-exit` - Cleanup automático em exit
- ❌ `vm/mmap-clean` - Write-back de páginas dirty
- ❌ `vm/mmap-remove` - Remover arquivo mapeado
- ❌ `vm/mmap-shuffle` - Acesso aleatório em MMAP

---

### 🌿 Branch 5: vm-mmap-validation
**Objetivo**: Implementar validações de mmap e casos de erro/edge cases

#### Arquivos Modificados
<!-- Preencher após implementação -->

#### Design
<!-- Descrever validações de addr/fd, detecção de overlaps, e casos especiais -->

#### Resultados de Testes

**Validação de Argumentos:**
- ❌ `vm/mmap-bad-fd` - File descriptor inválido
- ❌ `vm/mmap-null` - Endereço NULL
- ❌ `vm/mmap-zero` - Arquivo vazio (length = 0)
- ❌ `vm/mmap-misalign` - Endereço não page-aligned

**Detecção de Overlaps:**
- ❌ `vm/mmap-twice` - Mapear mesmo arquivo duas vezes
- ❌ `vm/mmap-overlap` - Mappings sobrepostos
- ❌ `vm/mmap-over-code` - Tentar mapear sobre código
- ❌ `vm/mmap-over-data` - Tentar mapear sobre dados
- ❌ `vm/mmap-over-stk` - Tentar mapear sobre stack

**Edge Cases:**
- ❌ `vm/mmap-inherit` - Filho não herda mappings do pai

---

### 📊 Resumo de Implementação

**Branches Planejadas:**
1. ✏️ `vm-infrastructure` - Frame Table + SPT + Stack Growth (9 testes)
2. ✏️ `vm-lazy-loading` - Lazy loading de executáveis (7 testes)
3. ✏️ `vm-swap-eviction` - Swap + Eviction (retestes de paging)
4. ✏️ `vm-mmap-basic` - Memory mapping básico (8 testes)
5. ✏️ `vm-mmap-validation` - Validações e edge cases (13 testes)

**Total de Testes VM-específicos**: 28 testes (atualmente 5/28 passando)

**Testes herdados do Projeto 2**: 80 testes userprog + 13 testes filesys/base = 93 testes

**Total Projeto 3**: 113 testes (atualmente 85/113 passando - 75.2%)