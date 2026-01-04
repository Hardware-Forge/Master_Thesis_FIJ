# FIJ - Fault Injection Tool

FIJ is a kernel-space fault injection tool designed for running automated injection campaigns on target programs.

## Overview

FIJ consists of two main components:
- **User-space program**: Reads configuration and automates injection calls
- **Kernel-space module**: Executes programs and handles fault injection

## Prerequisites

1. Linux kernel version **> 6.14**
2. Two C++ libraries (automatically installed via Makefile)

## Installation

The tool uses a Makefile with three main commands:

### `make install`
Performs complete setup:
- Pulls and installs C++ dependencies
- Compiles and installs the kernel module
- Compiles the userspace program

### `make uninstall`
Removes all installed components:
- Removes kernel module
- Cleans compiled files

### `make start`
Runs injection campaigns:
- Reads from `fij_runner/config.json` by default
- Custom config path can be specified by modifying the `CONFIGJSON` parameter in the Makefile

## Configuration

The `config.json` file controls injection campaigns. **All paths must be absolute/full paths.**

### Configuration Structure

```json
{
  "workers": 4,      //workers is the number of threads that the user wants to use to parallelize the injections
  "base_path": "/home/andrea/Desktop",          //this is a helper parameter to specify paths more easily. The reason is that in this file the FULL PATHS have
                                                //be specified
  "baseline_runs": 200 ,                        //--> optional parameter. If not applied the baseline_runs are 100
  "defaults": {                                 ///
    "runs": 1,                                  ///--->this default can be used to run the injection with some specific parameters. This applies the params to all campaigns
    "weight_mem": 15                            ///
  },
  "targets": [
    {
      "path": "{base_path}/python/filter.py",           //this is the FULL PATH of the program to do injection in

      "defaults": {                             ///
        "runs": 20000,                          ///--->these parameters are specific for the injections on this path. They overwrite the general parameters
        "weight_mem": 19                        ///
      },

      "args": [
        {
            "value": "{base_path}/python/image1.png   {campaign}/injection_{run}/result.png   -f blur"
        },
        {
            "value": "{base_path}/python/image1.png   {campaign}/injection_{run}/result.png   -f sharp"
        },
        {
            "value": "{base_path}/python/image2.png   {campaign}/injection_{run}/result.png   -f blur"
        },
        {
            "value": "{base_path}/python/image2.png   {campaign}/injection_{run}/result.png   -f sharp"
        }
      ]
    },
    { 
      "path": "{base_path}/pythonvenv/venv/yolo.py",
      "defaults": {
        "runs": 20000
      },
      "args":[ 
        {"value": "{base_path}/pythonvenv/venv/image1.jpg"},
        {"value": "{base_path}/pythonvenv/venv/image2.jpg"},
        {"value": "{base_path}/pythonvenv/venv/image3.jpg"},
        {"value": "{base_path}/pythonvenv/venv/image4.jpg"}
      ]
    }
  ]
}
```

### Configuration Parameters

#### Global Settings
- **`workers`**: Number of parallel threads for injection execution
- **`base_path`**: Helper variable for constructing full paths (use `{base_path}` in paths)
- **`baseline_runs`**: Number of baseline runs to determine average run time of the process. It is **<span style="color: orange;">OPTIONAL</span>** and if not specified 100 baseline runs are executed
- **`defaults`**: Default parameters applied to all campaigns (can be overridden per target)

#### Target Settings
- **`path`**: Full path to the target program
- **`defaults`**: Target-specific parameters that override global defaults
- **`args`**: Array of argument sets to test with the target

#### Injection Parameters
- **`runs`**: Number of injection runs to perform
- **`weight_mem`**: Weight for memory injection probability
  - `weight_mem=19` → 5% (1/20) probability of register injection
  - `weight_mem=15` → 6.25% (1/16) probability of register injection
  - **<span style="color: red;">If not specified, injections only target registers</span>**

### Output Redirection

Use `{campaign}` and `{run}` placeholders to redirect output files:
- **`{campaign}`**: Automatically uses generated filepath+args
- **`{run}`**: Current injection run number
- **Example**: `--output {campaign}/injection_{run}/result.png`

## Output Structure

Logs are stored in the `fij_logs` folder in the parent directory:

```
ParentFolder/
├── FijFolder
|   ├──fij/
|   ├── fij_runner/
|   ├── Makefile
└── fij_logs/
    └── filepath+args_campaign/
        ├── injection_0/
        │   ├── log.txt          # STDOUT & STDERR
        │   └── result.png       # Program output
        ├── injection_1/
        │   ├── log.txt
        │   └── result.png
        └── ...
```

Each campaign (file + args combination) generates its own folder. STDOUT and STDERR are automatically redirected to `log.txt` in each injection folder.

## Advanced Parameters

Complete list of available injection parameters:

```javascript
{
  "reg": "rax",              // Register name (architecture-specific: rax, r10, etc.)
  "reg_bit": 0,              // Bit position to flip (0-63)
  "weight_mem": 15,          // Memory injection weight
  "only_mem": 1,             // Inject only in memory (0 or 1)
  "min_delay_ms": 0,         // Minimum delay (defaults to 0)
  "max_delay_ms": 1000,      // Maximum delay (auto-computed in baseline)
  "pc": 12345,               // Program counter offset from start_code
  "thread": 0,               // Thread index (0, 1, 2, ...)
  "all_threads": 1,          // Inject in all threads (0 or 1)
  "nprocess": 2,             // Process order in tree (root-lchild-rchild...)
  "no_injection": 0          // Skip injection for baseline runs (0 or 1)
}
```

These parameters can be specified in any of the "defaults" sections.

## Example Usage

1. Install the tool:
   ```bash
   make install
   ```

2. Configure `config.json` with your target programs and parameters

3. Run injection campaign:
   ```bash
   make start
   ```

4. Results will be available in `fij_logs/`

5. When finished, a "diff" folder will be available inside each filepath+args folder. Inside it there will be diff_ith of only the failure runs and a csv file summary with a report at the end.

### Example 1: Coremark
Scenario: Running a standard C/C++ executable that prints results to STDOUT. Setup: Ensure coremark.exe is compiled and available in your target folder.
The example below will consider the target folder in Desktop
```json
{
  "workers": 4,
  "base_path": "/home/andrea/Desktop",
  "defaults": {
    "runs": 1000,
    "weight_mem": 1
  },
  "targets": [
    {
      "path": "{base_path}/coremark-main/coremark.exe",
      "args": [
        { "value": "0x0 0x0 0x66 400000 7 1 2000" }
      ]
    }
  ]
}
```
This config.json does an injection campaign of 1000 runs of the coremark.exe program using as arguments "0x0 0x0 0x66 0 7 1 2000" and making the probability of injecting into memory 50% of the times.

### Example 2: Mnist
Scenario: Running a Python script inside a virtual environment.
This example takes into consideration the case in which we have to run a python script.
This program takes executables as input to run injection campaigns. A solution to run python scripts is to add the following line at the top of the script that needs to be executed.
```python
!#absolute/path/to/specific/python/interpreter/executable
```
and then execute the
```bash
chmod +x /path/to/python/script.py
```
Only after these instructions are executed can the script be executed successfully to the injection program.
```json
{
  "workers": 4,
  "base_path": "/home/andrea/Desktop",
  "defaults": {
    "runs": 1000,
    "weight_mem": 0
  },
  "targets": [
    {
      "path": "{base_path}/pythonvenv/mnist.py",
      "args": [
        { "value": "{base_path}/pythonvenv/source_image.jpg" }
      ]
    },
    {
      "path": "{base_path}/pythonvenv/mnist.py",
      "defaults": { "weight_mem": 1 },
      "args": [
        { "value": "{base_path}/pythonvenv/source_image.jpg" }
      ]
    },
    {
      "path": "{base_path}/pythonvenv/mnist.py",
      "defaults": { "only_mem": 1 },
      "args": [
        { "value": "{base_path}/pythonvenv/source_image.jpg" }
      ]
    }
  ]
}
```
The provided config.json runs 3 injection campaigns of 1000 runs each. The first campaign does injections only in the registers, the second has a 50% chance of injecting in memory and the last one injects only in memory.

### Example 3: image_filter
Scenario: Running code that produces an output in addition to the STDOUT.
We are running a program that takes an image as input and outputs the blurred image in the specified path by the user.
as in the Example 2 we are running a python script so first of all we need to add the following line to the script image_filter.py
```python
!#home/user/pythonvenv/bin/python3
```
then opening the terminal from the folder in which the python script is we execute
```bash
chmod +x /path/to/python/image_filter.py
```
now here's an example of a config.json that redirects the output of the image into the experiment's folder.

```json
{
  "workers": 4,
  "base_path": "/home/andrea/Desktop",
  "defaults": {
    "runs": 1000,
    "weight_mem": 9
  },
  "targets": [
    {
      "path": "{base_path}/pythonvenv/image_blur.py",
      "args": [
        { "value": "--input {base_path}/pythonvenv/image_to_blur.jpg --output_path {campaign}/injection_{run}" }
      ]
    }
  ]
}
```
### Example 4: Multiple Injection Campaigns
Scenario: We want to run multiple programs by configuring the config.json file with different runs for each program

```json
{
  "workers": 4,
  "base_path": "/home/andrea/Desktop",
  "defaults": {
    "runs": 1000,
    "weight_mem": 9
  },
  "targets": [
    {
      "path": "{base_path}/pythonvenv/image_blur.py",
      "args": [
        { "value": "--input {base_path}/pythonvenv/image_to_blur.jpg --output_path {campaign}/injection_{run}" }
      ]
    },
    {
      "path": "{base_path}/pythonvenv/mnist.py",
      "defaults": {
        "runs": 20000,
        "weight_mem": 1,
      },
      "args": [
        { "value": "{base_path}/pythonvenv/source_image.jpg" }
      ]
    },
    {
      "path": "{base_path}/coremark-main/coremark.exe",
      "defaults": {
        "runs": 500,
      },
      "args": [
        { "value": "0x0 0x0 0x66 400000 7 1 2000" }
      ]
    }
  ]
}
```
in this config file we are running first a campaign of 1000 iterations with 90% chance of injection in memory for the image_blur script, then a campaign of the mnist.py script of 20000 iterations with 50% chance of memory injections and in the end we are running a campaign of 500 iterations of the coremark.exe program with chance of injection in memory equal to 90%.

## Notes
- **If the program that is being tested prints non deterministic parameters such as the execution time the analysis performed will likely show an absurd amout of Silent Data Corruptions (SDC). Before proceding the user should edit the program**
- All paths in `config.json` must be absolute paths
- Use `{base_path}` variable to simplify path specification
- Target-specific defaults override global defaults
- Each target can have multiple argument sets for comprehensive testing
