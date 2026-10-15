# ND-100X Interactive Shell - Example Script
# Save this as: commands.sh
# Run with: ./build/bin/nd100x --monitor --nd100-root=/path/to/bpun --script=commands.sh

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
