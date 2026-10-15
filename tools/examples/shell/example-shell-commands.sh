# ND-100X Interactive Shell - Example Script
# Run from the repo root with:
#   ./build/bin/nd100x --monitor --nd100-root=DIR --script=tools/examples/shell/example-shell-commands.sh
# or name it in the script key of example-shell-config.ini next to this file.
# Lines starting with '#' are comments; one shell command per line.

# Show available commands
HELP

# List all BPUN files
LIST-FILES *.bpun

# List all program files
LIST-FILES *.prog

# Load and run a specific program
# (replace 'kernel' with actual filename, without extension)
# RUN-PROGRAM kernel.bpun

# Display CPU registers after execution
# SHOW-REGISTERS

# Exit the shell
EXIT
