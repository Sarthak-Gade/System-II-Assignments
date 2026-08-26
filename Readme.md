# Opcode and Operand Analyzer

## Description

This C program reads an opcode details file and an assembly program file. It extracts the mnemonics and operands used in the assembly program and identifies the type of each operand.

## Requirements

* Opcode details file
* Assembly program file

## Compile

```bash
gcc opcodechk.c
```

## Run

The filenames are provided at runtime:

```bash
./a.out <opcode_file> <assembly_file>
```

Example:

```bash
./a.out opcode.txt check.asm
```

The filenames do not need to be fixed. Any valid opcode file and assembly file can be provided.

## Operand Types

The program identifies the following operand types:

* `REGISTER` — CPU registers such as `eax`, `ebx`, `esi`
* `CONSTANT` — Numeric values such as `10`, `-5`, `0x20`
* `MEMORY` — Operands containing memory addressing brackets such as `[eax]` or `dword[A]`
* `SYMBOL` — Labels or symbolic names such as `msg`, `lp`, `endp`

## Input

### Opcode File

Contains opcode information from the opcode table.

### Assembly File

Contains the assembly program to be analyze.

## Output

The program displays the mnemonic, operand, and operand type.

