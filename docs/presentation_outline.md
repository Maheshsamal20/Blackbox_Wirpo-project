# Presentation outline (about 10 minutes + demo)

1. **Problem (1 min)** – crashes destroy the evidence; show a plain log that is missing the last seconds.
2. **Idea (1 min)** – the aircraft black box; one slide with the architecture picture from `03_design.md`.
3. **Requirements and plan (1 min)** – top functional requirements, timeline, risks.
4. **Design (2 min)** – ring buffer with per-file cursors, locking rules, state machine, sequence diagram.
5. **Implementation tour (2 min)** – driver (`write/read/poll/ioctl`), daemon threads, snapshot flow, `bbctl`.
6. **Live demo (2 min)** – `sudo scripts/demo.sh`; then `bbctl stats` and `cat /proc/blackbox`.
7. **Testing (1 min)** – unit + sanitizers + integration + kernel smoke/stress results table.
8. **Results, limitations, future work (1 min)** – see `06_final_report.md`.

Demo tips: practise once in the VM, keep a second terminal on `dmesg -w`, have a recorded fallback in case the module fails to load.
