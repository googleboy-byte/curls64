# AI Usage — Prompts Log

> Every non-trivial AI interaction is logged here with context, prompt, response summary, and what was used or discarded.

---

## 2026-06-24 · SMP concurrency audit — race conditions in AP startup and TLB shootdown

**Prompt:**
Reviewed a set of 4 potential issues + 3 minor suggestions flagged against the latest SMP commits (ap_ready_flags atomicity, LAPIC_EOI magic constant, dirty pending_mask on shootdown timeout, lock-free handler reads, MAX_CPUS drift, test coverage gap, busy-delay calibration). Asked AI to cross-check each against the actual codebase, discard hallucinated issues, and produce an implementation plan for the real ones.

**Response summary:**
AI verified all 4 main issues against the source. Confirmed 3 as real bugs (non-atomic `ap_ready_flags |=`, dirty `pending_mask` on timeout, magic `0x0B0` constant) and 1 as a valid-but-safe concern needing documentation (lock-free handler read). Confirmed the `MAX_CPUS` drift hazard as real. Correctly discarded the test coverage and TSC delay items as not actionable. Produced a targeted implementation plan with file-level diffs.

**What we used / didn't use:**
Used all 5 proposed fixes directly — they were mechanical and clearly correct after code verification. The protocol invariant comment for issue #4 was adopted as-is since the ordering analysis was accurate. Discarded the test coverage and TSC delay suggestions per the plan (acknowledged, not worth the complexity right now).
