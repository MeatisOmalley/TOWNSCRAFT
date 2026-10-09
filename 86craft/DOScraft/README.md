# DOScraft

IBM-PC port of Townscraft, intended for a late-1989 386DX/33-class PC in 86Box.

Current stage: research, architecture and reproducible house-rendering investigation. There is no runnable DOS game yet.

See [the port plan](PORT_PLAN.md) for hardware qualifications, the DOS platform, renderer candidates, large-world storage, tests and milestone gates. [The investigation runner](tools/audit_towns_house.py) builds isolated Towns test snapshots without modifying Towns production sources or ISOs.

The emulator checkout at `../86Box` is an independent Git repository. Game work belongs here. Generated builds and mutable runtime images are ignored.
