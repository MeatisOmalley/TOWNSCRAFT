# Emulator testing

Close DOS emulator windows opened for testing as soon as their tests are finished.
Do not leave completed diagnostic VMs running. Preserve their disk images and
reports. Do not close an unrelated or still-active user session.
Closing a finished test window takes one close-button click; do not restart
computer use solely to verify closure when the user says they closed it.
For a completed, idle diagnostic with its report written, stopping its verified
emulator PID is allowed and avoids computer-use overhead. Check the executable
path and exact private VM command line first; never kill all emulator processes.
Use graceful shutdown when a live game/save or pending disk work may be present.
