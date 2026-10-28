# Held-out set blind-1

20 search scenarios written by a separate writer who read only `heldout/WRITING_SCENARIOS.md`, in
an empty folder, without seeing the code, the configuration, the development scenarios or any
result. The files are used exactly as written. The set runs once (`sar heldout --set
heldout/blind-1`); that first run is `reports/heldout/blind-1/first_run.json`, and the set and the
run are pinned by sha256 in `tests/test_heldout.py`. Nothing in the planners, policy or
configuration is changed to pass it. If its failures are ever studied, `STATUS` becomes
`regression` and a new blind set is written from the guide.
