import FlWqCode

/-!
# Workqueue model (FL-25)

A small model of the durable job state machine in kernel/core/sched/workqueue.c, the staging bounds, and the
session-key publication rule in kernel/core/identity/session.c. The proofs are about the model; the C is checked
against the model by tests (tests/test_wq_model.c runs the generated `fl_wq_transition_allowed` over every
transition the real queue issued). State numbers are fl_wq_state_t: appended, never renumbered.
-/

namespace FlWq

inductive St where
  | pending | running | done | failed | expired | held | queued | skipped
  deriving DecidableEq, Repr

def St.code : St → UInt8
  | .pending => 1 | .running => 2 | .done => 3 | .failed => 4
  | .expired => 5 | .held => 6 | .queued => 7 | .skipped => 8

def St.ofCode? : UInt8 → Option St
  | 1 => some .pending | 2 => some .running | 3 => some .done | 4 => some .failed
  | 5 => some .expired | 6 => some .held | 7 => some .queued | 8 => some .skipped
  | _ => none

def terminal : St → Bool
  | .done | .failed | .expired | .skipped => true
  | _ => false

/-- The compare-and-set moves the queue makes (`from` → `to`), and no others. -/
def allowed : St → St → Bool
  | .pending, .queued  => true  -- claimed for the in-memory window
  | .pending, .held    => true  -- recovery: a RECOVER_ASK job waits for its owner
  | .pending, .expired => true  -- recovery: stale work is never run silently
  | .queued,  .running => true  -- RUNNING is written before the handler
  | .queued,  .pending => true  -- staging failed, or recovery of a staged job
  | .queued,  .expired => true
  | .queued,  .failed  => true  -- no handler is registered for the tag
  | .running, .done    => true
  | .running, .pending => true  -- retry
  | .running, .failed  => true
  | .held,    .pending => true  -- the owner chose to run it
  | .held,    .skipped => true  -- the owner declined this occurrence
  | _, _ => false

theorem terminal_is_final : ∀ s t, terminal s = true → allowed s t = false := by
  intro s t h; cases s <;> cases t <;> simp_all [terminal, allowed]

theorem no_self_move : ∀ s, allowed s s = false := by
  intro s; cases s <;> simp [allowed]

/-- A handler runs only after a successful QUEUED → RUNNING move. -/
theorem running_only_from_queued : ∀ s, allowed s .running = true → s = .queued := by
  intro s h; cases s <;> simp_all [allowed]

theorem done_only_from_running : ∀ s, allowed s .done = true → s = .running := by
  intro s h; cases s <;> simp_all [allowed]

theorem held_only_from_pending : ∀ s, allowed s .held = true → s = .pending := by
  intro s h; cases s <;> simp_all [allowed]

/-! ## A job never runs more than `max` times -/

structure Job where
  state : St
  attempts : Nat
  max : Nat

/-- One move, with the attempt accounting of workqueue.c: entering RUNNING counts an attempt, and a failed
    attempt goes back to PENDING only while attempts remain (otherwise FAILED). -/
inductive Step : Job → Job → Prop where
  | claim (j : Job) (h : j.state = .pending) : Step j { j with state := .queued }
  | unclaim (j : Job) (h : j.state = .queued) : Step j { j with state := .pending }
  | start (j : Job) (h : j.state = .queued) (hlt : j.attempts < j.max) :
      Step j { j with state := .running, attempts := j.attempts + 1 }
  | finish (j : Job) (h : j.state = .running) : Step j { j with state := .done }
  | retry (j : Job) (h : j.state = .running) (hlt : j.attempts < j.max) : Step j { j with state := .pending }
  | giveUp (j : Job) (h : j.state = .running) (hge : j.max ≤ j.attempts) : Step j { j with state := .failed }
  | hold (j : Job) (h : j.state = .pending) : Step j { j with state := .held }
  | release (j : Job) (h : j.state = .held) : Step j { j with state := .pending }

def Inv (j : Job) : Prop :=
  j.attempts ≤ j.max ∧ ((j.state = .pending ∨ j.state = .queued ∨ j.state = .held) → j.attempts < j.max)

theorem step_preserves : ∀ j j', Step j j' → Inv j → Inv j' := by
  intro j j' hs hi
  obtain ⟨h1, h2⟩ := hi
  cases hs with
  | claim h => have := h2 (Or.inl h); simp [Inv]; omega
  | unclaim h => have := h2 (Or.inr (Or.inl h)); simp [Inv]; omega
  | start h hlt => simp [Inv]; omega
  | finish h => simp [Inv]; omega
  | retry h hlt => simp [Inv]; omega
  | giveUp h hge => simp [Inv]; omega
  | hold h => have := h2 (Or.inl h); simp [Inv]; omega
  | release h => have := h2 (Or.inr (Or.inr h)); simp [Inv]; omega

/-- Reachable configurations from a fresh job (PENDING, no attempts, `max` ≥ 1). -/
inductive Reach : Job → Prop where
  | fresh (m : Nat) (hm : 0 < m) : Reach { state := .pending, attempts := 0, max := m }
  | next (j j' : Job) : Reach j → Step j j' → Reach j'

theorem reach_inv : ∀ j, Reach j → Inv j := by
  intro j hr
  induction hr with
  | fresh m hm => simp [Inv]; omega
  | next j j' _ hs ih => exact step_preserves j j' hs ih

/-- No reachable job has made more than `max` attempts. -/
theorem attempts_bounded : ∀ j, Reach j → j.attempts ≤ j.max := fun j h => (reach_inv j h).1

/-! ## Staging bounds: reserve, claim, publish -/

structure Q where
  pq : Nat        -- items in the multilevel queue
  timers : Nat    -- delayed items
  reserved : Nat  -- slots promised to a durable job being claimed
  durable : Nat   -- durable items staged in the queue
  prot : Nat      -- protected items (they may use the reserve)

structure Caps where
  pendingMax : Nat
  reserve : Nat
  half : Nat

/-- Ordinary and durable work together never pass `pendingMax - reserve`; only protected items use the rest. -/
def QInv (c : Caps) (q : Q) : Prop :=
  q.durable ≤ q.pq ∧ q.prot ≤ q.pq ∧
  (q.pq - q.prot) + q.timers + q.reserved ≤ c.pendingMax - c.reserve ∧
  q.durable + q.reserved ≤ c.half

def reserveOk (c : Caps) (q : Q) : Prop :=
  q.durable + q.reserved < c.half ∧ (q.pq - q.prot) + q.timers + q.reserved < c.pendingMax - c.reserve

def reserve (q : Q) : Q := { q with reserved := q.reserved + 1 }
/-- Publishing a claimed record turns its reservation into a staged item. -/
def publish (q : Q) : Q := { q with reserved := q.reserved - 1, pq := q.pq + 1, durable := q.durable + 1 }
def release (q : Q) : Q := { q with reserved := q.reserved - 1 }

theorem reserve_preserves (c : Caps) (q : Q) (h : QInv c q) (ok : reserveOk c q) : QInv c (reserve q) := by
  obtain ⟨a, b, d, e⟩ := h; obtain ⟨o1, o2⟩ := ok; simp [QInv, reserve]; omega

theorem publish_preserves (c : Caps) (q : Q) (h : QInv c q) (hr : 0 < q.reserved) : QInv c (publish q) := by
  obtain ⟨a, b, d, e⟩ := h; simp [QInv, publish]; omega

theorem release_preserves (c : Caps) (q : Q) (h : QInv c q) : QInv c (release q) := by
  obtain ⟨a, b, d, e⟩ := h; simp [QInv, release]; omega

/-- An ordinary submit checks `reserved` too, so it cannot take a slot a claim has already promised. -/
def submitOk (c : Caps) (q : Q) : Prop := (q.pq - q.prot) + q.timers + q.reserved < c.pendingMax - c.reserve

theorem submit_preserves (c : Caps) (q : Q) (h : QInv c q) (ok : submitOk c q) :
    QInv c { q with pq := q.pq + 1 } := by
  obtain ⟨a, b, d, e⟩ := h; simp [QInv, submitOk] at *; omega

/-! ## A key derived for one login is published only while that login is current -/

structure Sess where
  gen : Nat
  resident : Option Nat  -- the login generation whose key is online

def login (s : Sess) : Sess := { s with gen := s.gen + 1 }
def logout (s : Sess) : Sess := { gen := s.gen + 1, resident := none }
/-- Publication under the session lock: refused for any generation but the current one. -/
def publishKey (g : Nat) (s : Sess) : Sess := if g = s.gen then { s with resident := some g } else s

theorem stale_publish_refused (g : Nat) (s : Sess) (h : g ≠ s.gen) : publishKey g s = s := by
  simp [publishKey, h]

theorem logout_then_late_publish (s : Sess) : publishKey s.gen (logout s) = logout s := by
  simp [publishKey, logout]

/-- Publishing can only make the current generation's key resident; any other resident key was already there. -/
theorem publish_resident_is_current (g r : Nat) (s : Sess) (h : (publishKey g s).resident = some r) :
    (r = g ∧ g = s.gen) ∨ s.resident = some r := by
  unfold publishKey at h
  split at h
  · rename_i hg; left; simp at h; exact ⟨h.symm, hg⟩
  · right; exact h

/-! ## The C interface agrees with the model

`FlWqCode` is what the C side calls (plain `UInt8` functions, so the generated C needs no Lean runtime).
These theorems say it is the model's `allowed` and `terminal`, and that a number that is no state is refused. -/

theorem code_allowed_matches (a b : St) :
    FlWqCode.transitionAllowed a.code b.code = (if allowed a b then 1 else 0) := by
  cases a <;> cases b <;> decide

theorem code_terminal_matches (a : St) :
    FlWqCode.stateTerminal a.code = (if terminal a then 1 else 0) := by
  cases a <;> decide

theorem code_refuses_non_states (f t : UInt8) (h : St.ofCode? f = none ∨ St.ofCode? t = none) :
    FlWqCode.transitionAllowed f t = 0 := by
  have hf : ∀ x : UInt8, St.ofCode? x = none → (x ≠ 1 ∧ x ≠ 2 ∧ x ≠ 6 ∧ x ≠ 7) := by
    intro x hx
    refine ⟨?_, ?_, ?_, ?_⟩ <;> (rintro rfl; simp [St.ofCode?] at hx)
  have ht : ∀ x : UInt8, St.ofCode? x = none →
      (x ≠ 1 ∧ x ≠ 2 ∧ x ≠ 3 ∧ x ≠ 4 ∧ x ≠ 5 ∧ x ≠ 6 ∧ x ≠ 7 ∧ x ≠ 8) := by
    intro x hx
    refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩ <;> (rintro rfl; simp [St.ofCode?] at hx)
  rcases h with h | h
  · obtain ⟨a, b, c, d⟩ := hf f h
    simp [FlWqCode.transitionAllowed, a, b, c, d]
  · obtain ⟨a, b, c, d, e, g, i, k⟩ := ht t h
    simp [FlWqCode.transitionAllowed, a, b, c, d, e, g, i, k]

end FlWq
