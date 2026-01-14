# GitHub Actions Learning Notes

## Learning journey
- Asked the AI how to scope a workflow to a single folder without triggering on other assignments and learned about the `paths` filter on both `push` and `pull_request`.
- Clarified whether `make` is available on `ubuntu-latest`; learned it is preinstalled so the workflow could rely on it instead of adding extra setup.
- Discovered `defaults.run.working-directory` keeps every step inside `0-Warmup/`, avoiding repeated `cd` commands and mistakes.
- Debugged an early failure where `pytest` was missing by adding an explicit `pip install pytest` step.

### Example prompts I used
1. "GitHub Actions: limit workflow to run only when files in 0-Warmup/ change"
2. "Do I need to install make on ubuntu-latest GitHub Actions runners?"
3. "How do I set a default working directory for all steps in a job?"

## Technical explanation of the workflow
- Triggers: `push` and `pull_request` events with `paths: 0-Warmup/**` so only changes inside this assignment start the job.
- Platform: runs on `ubuntu-latest` with a job named `build-and-test`.
- Setup: uses `actions/checkout@v4` to get the code, `actions/setup-python@v5` to provision Python 3.11, then installs `pytest` (the only required Python dependency).
- Testing: runs `pytest` from `0-Warmup/`. The tests themselves call `make clean` and `make`, so the C program is compiled before assertions run.

## Why it only runs for 0-Warmup changes
- The `paths` filter is applied to both event types; if a commit or PR does not touch `0-Warmup/**`, GitHub skips the workflow. This prevents runs triggered by other assignment folders.

## How to extend to other assignments
- Create additional workflow files under `.github/workflows/` with their own `paths` filters (e.g., `1-NextAssignment/**`) and adjust working directories and dependencies as needed.
- Alternatively, expand the existing workflow's `paths` list and update `working-directory` when you want a single job to cover more folders.

## Evidence to capture
- After pushing a change under `0-Warmup/`, take two screenshots: (1) the successful run in the Actions tab and (2) the job log showing checkout, Python setup, install, and pytest steps. Paste or link them here once captured.
