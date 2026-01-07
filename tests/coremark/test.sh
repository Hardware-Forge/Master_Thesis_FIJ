#!/bin/bash

CMD="./coremark.exe 0x0 0x0 0x66 800000 7 1 2000"
ITERATIONS=1100

echo "Starting $ITERATIONS iterations of CoreMark..."
echo "----------------------------------------"

start_time=$(date +%s.%N)

for ((i=1; i<=ITERATIONS; i++))
do
    # Run the command silently
    $CMD > /dev/null 2>&1
    
    # Calculate percentage
    percent=$(( 100 * i / ITERATIONS ))

    # Print progress on the same line using \r (Carriage Return)
    # -n: do not output the trailing newline
    # -e: enable interpretation of backslash escapes
    echo -ne "Status: Running iteration $i of $ITERATIONS ... ($percent%)\r"
done

# Move to a new line after the loop finishes
echo "" 

end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)

echo "----------------------------------------"
echo "Processing complete."
echo "Total time taken: $duration seconds"
