# Stealth-Hypervisor-Win64

**Stealth-Hypervisor-Win64** is a Type-1 UEFI hypervisor research project for Windows x64, focused on VMX-based virtualization, guest memory introspection, and bidirectional communication between the hypervisor and a Windows user-mode companion application.

> This project is designed for low-level cybersecurity research, Windows internals learning, virtualization experiments, and controlled lab environments.

---

## Overview

The goal of this project is to build a bare-metal hypervisor that boots from a UEFI environment, initializes Intel VT-x virtualization, launches Windows as a guest, and provides a custom communication channel between the hypervisor and a user-mode Windows application.

Unlike classic Windows kernel tooling, the project aims to explore hypervisor-level visibility without relying on a traditional Windows kernel driver as the main communication layer.

---

## Core Objectives

* Boot as a UEFI Type-1 hypervisor
* Initialize Intel VMX operation
* Launch Windows inside VMX non-root mode
* Handle VM exits from the guest
* Implement EPT-based memory management
* Build a guest-to-hypervisor communication mechanism
* Support hypervisor-to-user-mode messaging
* Explore Windows guest memory introspection
* Investigate process memory observation from the hypervisor layer

---

## Planned Architecture

```text
UEFI Bootloader
      |
      v
Type-1 Hypervisor
      |
      v
VMX Root Mode
      |
      v
Windows x64 Guest
      |
      v
User-Mode Companion Application
```

The hypervisor runs below Windows and controls the virtualized execution environment. Windows executes as the guest operating system, while the user-mode companion application communicates with the hypervisor through a custom communication mechanism.

---

## Main Components

### UEFI Loader

Responsible for loading the hypervisor before Windows starts.

Planned responsibilities:

* Initialize the UEFI environment
* Prepare memory regions
* Load the hypervisor core
* Transfer execution to the virtualization layer

---

### Hypervisor Core

The main virtualization layer.

Planned responsibilities:

* Enable VMX operation
* Configure VMCS structures
* Enter VMX root mode
* Launch Windows in VMX non-root mode
* Handle VM exits
* Manage CPU virtualization controls
* Maintain hypervisor state

---

### EPT Engine

The memory virtualization subsystem.

Planned responsibilities:

* Build Extended Page Tables
* Manage guest physical memory access
* Control page permissions
* Support memory introspection features
* Track selected guest memory regions

---

### Communication Layer

A custom IPC mechanism between the hypervisor and the Windows guest.

Possible approaches:

* Shared memory region
* CPUID-based trap communication
* VM-exit-triggered signaling
* Ring-buffer-based message queue

Planned message flow:

```text
Windows User-Mode App
        |
        v
Shared Communication Region
        |
        v
Hypervisor
```

And in the opposite direction:

```text
Hypervisor
        |
        v
Shared Communication Region
        |
        v
Windows User-Mode App
```

---

### Windows Companion App

A user-mode Windows application used to interact with the hypervisor.

Planned responsibilities:

* Send commands to the hypervisor
* Receive hypervisor responses
* Display guest memory information
* Request controlled memory inspection
* Act as the frontend for research features

---

## Future Features

* VMX initialization
* VMCS setup
* VM exit handler
* EPT identity mapping
* Guest memory read primitives
* Guest virtual address translation
* Windows CR3 / DTB tracking
* Process-aware memory introspection
* User-mode companion application
* Shared memory IPC
* Command protocol
* Debug logging system
* Multi-core support
* Safer introspection boundaries
* Documentation of internals

---

## Research Focus

This project focuses on the following areas:

* Hypervisor development
* Windows x64 internals
* Intel VT-x virtualization
* VMX root and non-root execution
* Extended Page Tables
* Guest memory introspection
* Low-level communication channels
* Offensive security research
* Anti-debugging and anti-tamper concepts
* Virtualization-based monitoring

---

## Security Notice

This project is intended strictly for:

* Personal research
* Educational purposes
* Authorized lab testing
* Defensive security research
* Windows internals exploration

Do not use this project on systems you do not own or do not have explicit permission to test.

The project is not intended for unauthorized access, malware development, cheating, evasion, or abuse.

---

## Difficulty Level

This project combines multiple advanced topics:

* UEFI development
* Low-level C/C++
* Assembly
* Windows internals
* CPU virtualization
* Memory management
* VM exits
* EPT
* Debugging without a traditional OS environment

It is expected to be significantly more complex than standard user-mode Windows security tools.

---

## Repository Status

Current status: **Research / Early Development**

The project is currently in the design and experimentation phase. The initial goal is to build a stable UEFI hypervisor skeleton before adding advanced introspection and communication features.

---

## Roadmap

### Phase 1 — UEFI Foundation

* Create UEFI application
* Print debug information
* Detect CPU virtualization support
* Prepare memory layout
* Build basic boot flow

### Phase 2 — VMX Initialization

* Enable VMX operation
* Allocate VMXON region
* Allocate VMCS region
* Configure required MSRs
* Enter VMX root mode

### Phase 3 — Guest Execution

* Configure guest state
* Configure host state
* Launch Windows guest
* Handle basic VM exits
* Resume guest execution safely

### Phase 4 — EPT Support

* Build EPT structures
* Identity-map guest memory
* Handle EPT violations
* Add memory permission controls
* Prepare memory introspection primitives

### Phase 5 — Communication Channel

* Create shared memory region
* Define message protocol
* Implement guest-to-hypervisor messages
* Implement hypervisor-to-guest responses
* Build user-mode companion application

### Phase 6 — Windows Introspection

* Track CR3 changes
* Resolve guest virtual addresses
* Identify process address spaces
* Read selected process memory
* Expose controlled results to the companion app

---

## Example Use Cases

* Learning Intel VT-x and VMX internals
* Understanding Type-1 hypervisor design
* Studying Windows guest memory layout
* Building a custom VMI research framework
* Exploring hypervisor-based monitoring
* Experimenting with secure guest communication

---

## Tech Stack

* C
* C++
* x86_64 Assembly
* UEFI
* Intel VT-x
* VMX
* EPT
* Windows x64

---

## Project Philosophy

The purpose of this project is to understand what happens below the operating system.

Instead of interacting with Windows from user-mode or kernel-mode only, this project explores the layer beneath the OS: the hypervisor layer.

The long-term objective is to build a powerful but controlled research framework for observing and understanding Windows execution from a lower privilege layer.

---

## Disclaimer

This repository is for educational and authorized research purposes only.

The author is not responsible for any misuse of this project. Use it only in environments where you have full authorization.

---

## Author

**Zibrian Cadinot**
Low-level programming, cybersecurity, Windows internals, and offensive security research.
