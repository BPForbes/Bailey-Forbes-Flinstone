/-!
# The part of the workqueue model the C code calls

Plain functions on `UInt8` (the fl_wq_state_t numbers), so the C that Lean generates for them needs no Lean
runtime objects. FlWq.lean proves they agree with the model.
-/

namespace FlWqCode

/-- fl_wq_state_t: 1 PENDING 2 RUNNING 3 DONE 4 FAILED 5 EXPIRED 6 HELD 7 QUEUED 8 SKIPPED. -/
@[export fl_wq_transition_allowed]
def transitionAllowed (f t : UInt8) : UInt8 :=
  if f == 1 && (t == 7 || t == 6 || t == 5) then 1        -- PENDING -> QUEUED, HELD, EXPIRED
  else if f == 7 && (t == 2 || t == 1 || t == 5 || t == 4) then 1  -- QUEUED -> RUNNING, PENDING, EXPIRED, FAILED
  else if f == 2 && (t == 3 || t == 1 || t == 4) then 1    -- RUNNING -> DONE, PENDING, FAILED
  else if f == 6 && (t == 1 || t == 8) then 1               -- HELD -> PENDING, SKIPPED
  else 0

/-- 1 for DONE, FAILED, EXPIRED, SKIPPED: the payload is wiped and the job never moves again. -/
@[export fl_wq_state_terminal]
def stateTerminal (c : UInt8) : UInt8 :=
  if c == 3 || c == 4 || c == 5 || c == 8 then 1 else 0

end FlWqCode
