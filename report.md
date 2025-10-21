# PintOS Projeto 1 - Relatório de Implementação

## Parte 1: Thread Sleep (Alarm Clock)

### Resumo
Implementação de bloqueio de threads e de um alarme para eliminar o busy-wait em `timer_sleep()`.

### src/threads/thread.h e src/threads/thread.c
- Adicionado o campo `int64_t wake_time` em `struct thread` para armazenar o tick de reativação.  
- Ajustado `init_thread()` para inicializar `wake_time` com zero.

### src/devices/timer.c
- Incluído `lib/kernel/list.h`.  
- Criada lista estática `sleep_list` para gerenciar threads adormecidas.  
- Implementada a função de comparação `wake_time_less()`.  
- Atualizado `timer_init()` para inicializar `sleep_list`.  
- Reescrito `timer_sleep()` para usar bloqueio de threads.  
- Alterado `timer_interrupt()` para acordar threads cujo `wake_time` já passou.

### Design

**Thread Sleep**  
1. A thread invoca `timer_sleep(ticks)`.  
2. Calcula `wake_time = current_ticks + ticks`.  
3. Desativa interrupções (seção crítica).  
4. Insere a thread em `sleep_list`, ordenada por `wake_time`.  
5. Bloqueia a thread (remove-a da fila de prontos, estado BLOCKED).  
6. Reativa interrupções.  
7. O escalonador executa a próxima thread pronta.

**Thread Wake-Up**  
1. A cada tick, a interrupção de timer é disparada.  
2. Incrementa o contador global `ticks`.  
3. Examina o início de `sleep_list`.  
4. Enquanto `wake_time <= current_ticks`:  
   - Remove a thread de `sleep_list`.  
   - Desbloqueia a thread (adiciona-a à fila de prontos, estado READY).  
5. Encerra ao encontrar uma thread com `wake_time` futuro.

### Resultados de Testes
- ✅ `alarm-single`: Funcionamento básico de sono.  
- ✅ `alarm-multiple`: Suporte a múltiplas threads adormecidas.  
- ✅ `alarm-simultaneous`: Acordar threads no mesmo tick.  
- ✅ `alarm-priority`: Sono não afeta a precedência de prioridade.  
- ✅ `alarm-zero`: Dormir por 0 ticks.  
- ✅ `alarm-negative`: Dormir por ticks negativos.

---

## Parte 2.1: Escalonador por Prioridade

Implementação de escalonamento preemptivo baseado em prioridades.

### src/threads/thread.h e src/threads/thread.c
- Criada a função de comparação `thread_priority_less()`.  
- Ajustados `thread_unblock()` e `thread_yield()` para inserir threads na `ready_list` em ordem de prioridade.  
- Acrescentadas verificações de preempção em `thread_create()` e `thread_set_priority()`.

### src/threads/synch.c
- Implementada a função de comparação `cond_priority_less()`.  
- Reescrito `sema_up()` para ordenar `sema->waiters` por prioridade e verificar preempção.  
- Reescrito `cond_signal()` para sinalizar o waiter de maior prioridade.

### Design

**Criação de Thread**  
1. `thread_create()` inicializa a nova thread e chama `thread_unblock()`.  
2. `thread_unblock()` insere em `ready_list`, preservando ordem por prioridade.  
3. Se a nova thread tiver prioridade maior que a atual, `thread_create()` provoca `thread_yield()`.

**Escalonamento**  
1. `thread_yield()` reinserta a thread atual em `ready_list` por prioridade (O(n)).  
2. O escalonador seleciona a primeira thread de `ready_list` (O(1)).

**Sincronização**  
1. Em `sema_down()`/`lock_acquire()`/`cond_wait()`, a thread é removida de `ready_list` e inserida em `waiters`.  
2. `cond_signal()` ordena e sinaliza o waiter de maior prioridade.  
3. `sema_up()` ordena `sema->waiters` antes de liberar e verifica preempção.

**Mudança de Prioridade**  
1. `thread_set_priority()` atualiza a prioridade e verifica se deve chamar `thread_yield()`.

### Testes
- ✅ `priority-change`: Alteração de prioridade refletida imediatamente.  
- ✅ `priority-fifo`: Respeito à ordem FIFO entre iguais.  
- ✅ `priority-preempt`: Preempção imediata ao surgir maior prioridade.  
- ✅ `priority-sema`: Semáforo acorda o waiter de maior prioridade.  
- ✅ `priority-condvar`: Condvar sinaliza o waiter de maior prioridade.

---

## Parte 2.2: Doação de Prioridade

Implementação de doação de prioridade para prevenir inversão.

### src/threads/thread.h e src/threads/thread.c
- Incluídos `int base_priority`, `struct list locks_held` e `struct lock *waiting_on` em `struct thread`.  
- `init_thread()` agora inicializa `base_priority`, `locks_held` e `waiting_on`.  
- Criada `thread_refresh_priority()` para recalcular a prioridade efetiva e verificar preempção.  
- `thread_set_priority()` modificado para alterar `base_priority`.

### src/threads/synch.h e src/threads/synch.c
- Adicionado `struct list_elem elem` em `struct lock` para rastrear `locks_held`.  
- Implementada `donate_chain()` para doação transitiva.  
- Reescrito `lock_acquire()`:  
  - Define `current->waiting_on` e doa prioridade ao holder.  
  - Após adquirir, limpa `waiting_on`, adiciona o lock em `locks_held` e atualiza `holder`.  
- Reescrito `lock_release()` para remover o lock de `locks_held` e chamar `thread_refresh_priority()`.

### Design
1. Cada thread mantém `base_priority`, lista de `locks_held` e o lock `waiting_on`.  
2. Ao bloquear em lock ocupado, doa prioridade ao holder e propaga pela cadeia `waiting_on`.  
3. A prioridade efetiva é o máximo entre `base_priority` e as doações de todos os locks em `locks_held`.  
4. Em `lock_release()`, remove-se a doação associada e recalcula-se a prioridade.  
5. Após qualquer alteração de prioridade, se houver thread pronta com maior prioridade, ocorre preempção.

### Testes
- ✅ `priority-donate-one`: Doação de prioridade simples.  
- ✅ `priority-donate-multiple`: Doação para múltiplos holders.  
- ✅ `priority-donate-multiple2`: Múltiplas doações simultâneas.  
- ✅ `priority-donate-nest`: Doação transitiva.  
- ✅ `priority-donate-sema`: Doação envolvendo semáforos.  
- ✅ `priority-donate-lower`: Doação de prioridade menor.  
- ✅ `priority-donate-chain`: Cadeia de doações transitivas.  
