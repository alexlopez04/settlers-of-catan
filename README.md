
# Settlers of Catan

This repository contains firmware and protocol definitions for the Settlers of Catan senior design project.

## Project Structure

- `firmware/`: Source code for two components:
	- `board/`: Main board firmware
	- `player/`: Player station firmware
- `proto/`: Protocol Buffer definitions and generation scripts

## Getting Started

1. **Install dependencies**
	 - PlatformIO and NanoPB are required.
	 - Example for macOS:
		 ```sh
		 brew install platformio nanopb
		 ```

2. **Generate Protocol Buffer files**
	 - Run the generation script:
		 ```sh
		 cd proto
		 ./generate.sh
		 ```

3. **Build firmware**
	 - Use PlatformIO to compile either board or player firmware:
		 ```sh
		 cd firmware/board  # or firmware/player
		 pio run
		 ```

## Notes

- Make sure all dependencies are installed before building.
- Generated Protobuf files are located in `proto_gen/` and should not be committed.
- Platform IO output is in `firmware/*/.pio/` and should also not be committed.
