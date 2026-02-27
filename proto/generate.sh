#!/bin/bash

# Make missing output directories
mkdir -p ../proto_gen/board
mkdir -p ../proto_gen/player

# NanoPB codegen for Board
protoc --nanopb_out=../proto_gen/board catan.proto

# NanoPB codegen for Player
protoc --nanopb_out=../proto_gen/player catan.proto
