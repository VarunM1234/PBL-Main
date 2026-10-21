# SmartRoute: An Intelligent Bus Route Management and Real-Time Tracking System

SmartRoute is an academic bus-booking, fleet-management, and simulated live-tracking project designed to demonstrate how passengers can search for bus services, select seats, manage bookings, and follow a bus journey through a map-based interface.

The project includes two implementations:

1. **React Web Prototype:** React, TypeScript, Vite, Node.js, Express, and Leaflet.
2. **C/C++ Implementation:** C data structures, a C++ application engine, HTML, CSS, JavaScript, and a C++ HTTP server.

Both implementations explore the same transportation-management problem but use different architectures, dependencies, and execution workflows.

> **Important:** SmartRoute is an academic demonstration project. Bus operators, schedules, fares, seats, and passenger records use sample data. GPS movement, traffic incidents, and payments are simulated. The current application does not provide verified real-bus GPS tracking or real payment processing.

---

## 📋 Project Information

| Field | Details |
|---|---|
| Project Title | SmartRoute: An Intelligent Bus Route Management and Real-Time Tracking System |
| Team Name | SmartRoute |
| Team ID | `DSCPP-III-2026-T228` |
| Institution | Graphic Era (Deemed to be University), Dehradun |
| Department | Computer Science & Engineering |
| Academic Session | 2026–2027 |
| Mentor | Ms. Shrishti Chomoli |
| Project Category | Project-Based Learning (PBL) |

---

## 📑 Table of Contents

1. [Introduction](#1-introduction)
2. [Problem Statement](#2-problem-statement)
3. [Objectives](#3-objectives)
4. [Project Versions](#4-project-versions)
5. [Features and Modules](#5-features-and-modules)
6. [Technology Stack](#6-technology-stack)
7. [System Architecture](#7-system-architecture)
8. [Project Structure](#8-project-structure)
9. [Requirements and Installation](#9-requirements-and-installation)
10. [Run the React Prototype](#10-run-the-react-prototype)
11. [Compile and Run the C/C++ Application](#11-compile-and-run-the-cc-application)
12. [Build the Standalone C++ Version](#12-build-the-standalone-c-version)
13. [Data Structures and Algorithms](#13-data-structures-and-algorithms)
14. [Booking Workflow](#14-booking-workflow)
15. [Route Planning and Live Map](#15-route-planning-and-live-map)
16. [Notifications and Delay Management](#16-notifications-and-delay-management)
17. [Data Storage](#17-data-storage)
18. [Testing and Evaluation](#18-testing-and-evaluation)
19. [Security and Privacy](#19-security-and-privacy)
20. [Known Limitations](#20-known-limitations)
21. [Troubleshooting](#21-troubleshooting)
22. [Future Enhancements](#22-future-enhancements)
23. [Team Contributions](#23-team-contributions)
24. [Academic Context](#24-academic-context)
25. [License and Third-Party Notices](#25-license-and-third-party-notices)

---

## 1. Introduction

SmartRoute is designed to demonstrate how bus booking, route management, fleet operations, and journey tracking can be combined in a single transportation platform.

Passengers can search for bus services, view schedules, select seats, apply demonstration coupons, complete a simulated checkout, and follow a bus journey on a map.

Drivers and operators can use the C/C++ implementation to demonstrate trip management, staff assignment, passenger manifests, boarding verification, and delay reporting. The administrative interface demonstrates fleet statistics, coupon management, delay reports, and revenue summaries.

The C/C++ implementation uses a shared application engine for both the console application and browser-based interface. This allows the core business rules to be implemented in C++ instead of being duplicated in JavaScript.

### Project Scope

- Bus search and schedule management.
- Seat selection and temporary seat reservations.
- Booking, simulated payment, cancellation, and refund calculations.
- Ticket generation and QR-code support.
- Route planning using graph algorithms.
- Simulated bus movement and arrival estimates.
- Driver and operator trip management.
- Fleet status and staff assignment.
- Notifications for selected journey events.
- Demonstration of data structures and algorithms.
- Browser interface connected to a C++ application server.

---

## 2. Problem Statement

Passengers may have difficulty finding available bus services, checking seat availability, estimating arrival times, and receiving information about delays.

Transport operators also need ways to organize schedules, assign drivers and conductors, monitor journeys, and manage passenger boarding.

General-purpose map applications provide navigation and geographic information, but they do not necessarily provide bus-specific booking, seat selection, passenger manifests, and stop-by-stop notifications in one interface.

SmartRoute explores a unified solution that combines bus booking, transportation management, and simulated journey tracking.

### Primary Users

- **Passengers:** Search buses, select seats, view tickets, and track journeys.
- **Drivers:** View assigned trips, update simulated locations, and report delays.
- **Operators:** Manage buses, staff assignments, schedules, and fleet status.
- **Administrators:** Monitor fleet operations, coupons, delay reports, and summary statistics.

### Initial Route Coverage

The original React prototype focuses on the Delhi–Jaipur corridor along NH-48.

The C/C++ implementation includes a broader sample catalogue of cities, routes, buses, and schedules.

The route coverage differs between implementations and should be checked against the corresponding sample data files.

---

## 3. Objectives

1. Develop a user-friendly interface for searching bus services.
2. Display sample schedules, fares, coach types, and seat availability.
3. Implement seat selection and temporary seat holds.
4. Demonstrate booking, simulated payment, ticket generation, and cancellation.
5. Display a map with simulated bus movement and journey progress.
6. Calculate estimated arrival times using the available simulation logic.
7. Demonstrate notifications for tolls, halts, and destination arrival.
8. Provide driver and operator workflows for trip management.
9. Demonstrate staff assignment and conflict prevention.
10. Implement graph algorithms for route and multi-leg journey planning.
11. Implement data structures manually in C.
12. Use a shared C++ application engine for console and web interfaces.
13. Document the installation, compilation, testing, and execution process.
14. Distinguish implemented functionality from simulated and planned functionality.

---

## 4. Project Versions

SmartRoute contains two related implementations. They are separate applications and should be installed and executed independently.

### 4.1 React Web Prototype

| Component | Technology |
|---|---|
| Frontend | React 19 |
| Programming Languages | TypeScript and JavaScript |
| Development Server | Vite 8 |
| Styling | Tailwind CSS 4 |
| Backend | Node.js and Express 4 |
| TypeScript Execution | `tsx` |
| Map | Leaflet and OpenStreetMap |
| AI Integration | Google Gemini through `@google/genai` |

The React prototype focuses on the passenger-facing web interface, simulated booking, seat selection, checkout, notifications, and map tracking.

Its AI functionality depends on a valid server-side `GEMINI_API_KEY`.

### 4.2 C/C++ Implementation

| Component | Technology |
|---|---|
| Data Structure Library | C |
| Application Engine | C++ |
| Browser Interface | HTML, CSS, JavaScript |
| HTTP Server | C++ |
| Build System | GNU Make |
| Compiler | GCC and G++ |
| Sample Data | CSV files |
| Local Persistence | Application save files |
| Map Rendering | Leaflet |
| Road Geometry | Public OSRM routing service |
| Database Server | Not required |

The C/C++ implementation provides two interfaces:

1. **Console Application:** Runs the core application logic in a terminal.
2. **Web Application:** Runs a C++ HTTP server and serves the browser interface.

Both interfaces use the same C++ application engine.

### Important Differences

- The React prototype uses Node.js and npm.
- The C/C++ application uses GCC, G++, and Make.
- The React prototype uses browser-side state for the current booking workflow.
- The C/C++ implementation includes local persistence through its application storage layer.
- The React prototype has server-side Gemini endpoints.
- The C/C++ Trip Assistant uses rule-based logic.
- Neither implementation currently provides verified live GPS from an actual bus.

---

## 5. Features and Modules

### 5.1 Passenger Module

- Search buses by origin, destination, and journey date.
- View available sample services.
- Filter and sort bus results.
- Select seater and sleeper seats.
- Select multiple seats and find adjacent seats.
- Hold seats temporarily.
- Join a waitlist when a service is sold out.
- Apply demonstration coupons.
- Calculate fares, discounts, and insurance costs.
- Complete simulated checkout.
- Generate booking references and tickets.
- Display QR codes in the C/C++ implementation.
- Print tickets or save them as PDF through the browser.
- Cancel bookings and preview refunds.
- View journey information and booking status.
- Receive in-app notifications.

### 5.2 Bus Search Module

The search module supports service lookup and filtering.

The original React prototype includes date, coach type, AC, sleeper, and price filters.

The C/C++ implementation additionally demonstrates:

- City-prefix suggestions.
- Search and sorting algorithms.
- Schedule lookup.
- Lazy creation of trip records.
- Route selection.
- Multi-leg journey planning.

### 5.3 Seat Management Module

The seat-management module demonstrates:

- Seater and sleeper layouts.
- Seat selection and deselection.
- Group-seat selection.
- Seat availability.
- Temporary seat holds.
- Waitlists for sold-out trips.
- Seat release through supported cancellation workflows.

The C/C++ implementation uses a bitset for seat states and a min-heap for hold-expiry scheduling.

The React prototype uses sample seat data and browser-side state. It does not implement a shared server-side seat lock between multiple passengers.

### 5.4 Booking and Payment Module

- Calculate booking fares.
- Apply supported coupons.
- Calculate taxes and insurance.
- Demonstrate full-payment and deposit workflows.
- Create booking references.
- Maintain booking status.
- Simulate payment attempts.
- Calculate cancellation refunds.
- Prevent supported duplicate operations within the application's implemented rules.

**Payment limitation:** No real payment provider is connected to the documented demonstration workflow. No money is collected.

### 5.5 Ticket Module

Tickets can display:

- Booking reference.
- Passenger and journey details.
- Selected seats.
- Fare information.
- Boarding and dropping points.
- Journey status.
- Route representation.
- QR code in the C/C++ implementation.

Ticket printing and PDF saving can use the browser's print functionality.

### 5.6 Driver Module

The C/C++ implementation demonstrates:

- Viewing assigned trips.
- Starting and finishing trips.
- Submitting simulated or manually entered coordinates.
- Validating location updates.
- Maintaining a location trail.
- Reporting delays and reasons.
- Viewing passenger manifests.
- Checking in passengers using QR text or a booking reference.

Real driver-device GPS streaming is not currently deployed.

### 5.7 Operator Module

The C/C++ implementation demonstrates:

- Viewing buses, drivers, conductors, and trips.
- Assigning staff to trips.
- Rejecting supported off-duty or overlapping assignments.
- Updating staff availability.
- Starting and finishing trips.
- Reporting delays.
- Viewing passenger manifests.
- Masking passenger phone numbers under implemented time rules.
- Marking buses as under maintenance.
- Undoing supported changes.
- Automatically assigning staff using a greedy strategy and min-heap.
- Viewing demonstration revenue calculations.

### 5.8 Administrator Module

The C/C++ web interface provides an administrative view for demonstrating:

- Fleet statistics.
- Sample fleet telemetry.
- Revenue summaries by corridor.
- Coupon management.
- Delay reports.
- Simulated traffic scans.

### 5.9 Trip Assistant

The C/C++ implementation includes a rule-based Trip Assistant.

It recognizes supported city names, partial city names, dates, and search preferences such as:

- Cheapest.
- Fastest.
- Morning departure.
- Sleeper service.

It uses the application's search and route-planning logic. It is not a generative AI system.

The React prototype contains Gemini integration endpoints that require valid API configuration.

### 5.10 Tracking and Notifications

- Display a simulated bus marker on a map.
- Show route stops and journey progress.
- Display a route trail.
- Support map-following behavior where implemented.
- Simulate traffic jams, tolls, halts, and delays.
- Notify passengers when configured route events occur.
- Display arrival notifications.
- Support a map fallback where available.

---

## 6. Technology Stack

### 6.1 React Prototype

| Layer | Technology |
|---|---|
| Frontend | React 19 |
| Languages | TypeScript and JavaScript |
| Build Tool | Vite 8 |
| Styling | Tailwind CSS 4 |
| Backend | Express 4 |
| Runtime | Node.js |
| TypeScript Execution | `tsx` |
| Mapping | Leaflet and OpenStreetMap |
| AI | Google Gemini through `@google/genai` |

### 6.2 C/C++ Implementation

| Layer | Technology |
|---|---|
| Data Structures | C |
| Application Logic | C++ |
| C++ Standard | C++17 for the documented standalone build |
| Web Interface | HTML, CSS, JavaScript |
| HTTP Server | C++ |
| Build System | GNU Make |
| Compiler | GCC and G++ |
| Data Files | CSV |
| Local Storage | Application save files |
| Map Library | Leaflet 1.9.4 |
| Road Routing | Public OSRM service |

The C/C++ implementation does not require Java, Python, Node.js, or a database server to run the native application.

---

## 7. System Architecture

### 7.1 React Prototype

```text
Passenger / Browser
        |
        v
   React + Vite
        |
        v
 Express Server
    server.ts
        |
        +------------------+
        |                  |
        v                  v
 Application APIs      Gemini API

Browser ----------------------> Map Tile Servers
```

### 7.2 C/C++ Implementation

```text
Passenger / Driver / Operator / Admin
                  |
                  v
         HTML / CSS / JavaScript
                  |
                  v
            C++ HTTP Server
                  |
                  v
          Shared C++ App Engine
                  |
       +----------+----------+
       |          |          |
       v          v          v
    Booking    Tracking    Fleet Ops
       |          |          |
       +----------+----------+
                  |
                  v
       Hand-written C Structures
                  |
       +----------+----------+
       |          |          |
       v          v          v
      Trie      Graph       Heap
                  |
                  v
           CSV / Save Files
```

The browser interface sends requests to the C++ server. The server invokes the shared application logic and returns the result to the browser.

### 7.3 Map Architecture

```text
Browser Tracking Screen
          |
          +----> Leaflet Map Renderer
          |
          +----> Map Tile Provider
          |
          +----> OSRM Routing Service
          |
          v
 Display Route, Stops, Bus Marker and Trail
```

Map tiles and public routing services require an internet connection. Their availability and usage policies are outside the application's control.

---

## 8. Project Structure

### 8.1 React Prototype

```text
PBL_1/
├── server.ts
├── index.html
├── package.json
├── package-lock.json
├── .env.example
├── .gitignore
├── src/
│   ├── App.tsx
│   ├── firebase.ts
│   └── components/
│       ├── LiveRouteMap.tsx
│       └── RouteMiniMap.tsx
├── frontend/
├── backend/
├── firestore.rules
└── README.md
```

| File | Purpose |
|---|---|
| `server.ts` | Express server and Gemini endpoints |
| `index.html` | HTML entry point and theme variables |
| `package.json` | Dependencies and npm scripts |
| `.env.example` | Example environment variables |
| `src/App.tsx` | Main React application |
| `src/components/LiveRouteMap.tsx` | Simulated tracking map |
| `src/components/RouteMiniMap.tsx` | Route preview and waypoint list |
| `src/firebase.ts` | Firebase helpers; not used by the current booking flow |
| `firestore.rules` | Firestore access rules for the planned integration |
| `frontend/` and `backend/` | Earlier prototypes or supporting files; verify actual imports before removing |

### 8.2 C/C++ Implementation

```text
SmartRoute/
├── Makefile
├── package.json
├── run-web.sh
├── run-web.cmd
├── main.cpp
├── core/
│   ├── ds_vector.c
│   ├── ds_list.c
│   ├── ds_queue.c
│   ├── ds_stack.c
│   ├── ds_hash.c
│   ├── ds_heap.c
│   ├── ds_trie.c
│   ├── ds_bitset.c
│   ├── ds_fenwick.c
│   ├── ds_ring.c
│   ├── ds_lru.c
│   ├── ds_graph.c
│   ├── ds_unionfind.c
│   ├── ds_sort.c
│   ├── ds_search.c
│   ├── geo.c
│   ├── sha256.c
│   └── qr.c
├── app/
│   ├── app.hpp
│   ├── models.hpp
│   ├── catalog.cpp
│   ├── auth.cpp
│   ├── seats.cpp
│   ├── search.cpp
│   ├── booking.cpp
│   ├── coupons.cpp
│   ├── payment.cpp
│   ├── tracking.cpp
│   ├── notifications.cpp
│   ├── vendor.cpp
│   ├── storage.cpp
│   ├── ticket.cpp
│   ├── basemap.cpp
│   ├── ops.cpp
│   ├── assistant.cpp
│   ├── cli.cpp
│   └── http_server.cpp
├── web/
│   ├── index.html
│   ├── basemap.json
│   └── vendor/
│       └── LEAFLET-LICENSE.txt
├── data/
│   ├── cities.csv
│   ├── points.csv
│   ├── routes.csv
│   ├── buses.csv
│   ├── schedules.csv
│   ├── staff.csv
│   └── save/
├── tests/
│   ├── test_core.c
│   ├── test_core2.c
│   ├── test_app.cpp
│   ├── test_ops.cpp
│   └── bench.cpp
├── tools/
│   ├── onefile.cpp
│   └── page_gen.cpp
└── docs/
    ├── DSA_REPORT.md
    └── PROJECT_PROMPT.md
```

This is a representative structure for the documented C/C++ implementation. File names and extensions must match the actual repository. Some modules may be header-only or use different extensions.

---

## 9. Requirements and Installation

### 9.1 React Prototype Requirements

- Node.js 20 or newer.
- npm.
- Git, if cloning the repository.
- Internet access for dependency installation and map resources.
- A Gemini API key if testing the AI endpoints.

Verify the installed tools:

```bash
node --version
npm --version
git --version
```

### 9.2 C/C++ Requirements

- GCC.
- G++.
- GNU Make.
- A compatible terminal.
- Internet access for online map tiles and road routing.

The native C/C++ application does not require Node.js, Java, Python, or a database server.

#### Windows Setup

The documented environment uses MSYS2 UCRT64.

1. Install MSYS2 from https://www.msys2.org/.
2. Open the **MSYS2 UCRT64** terminal.
3. Install the compiler and build tools.

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc make
```

4. Verify the tools.

```bash
gcc --version
g++ --version
make --version
```

5. Navigate to the project directory using the terminal's supported path syntax.

If the repository provides a `check-tools` target, verify it with:

```bash
make check-tools
```

#### Linux Setup

On Ubuntu or Debian:

```bash
sudo apt update
sudo apt install build-essential
```

Verify the tools:

```bash
gcc --version
g++ --version
make --version
```

#### macOS Setup

Install the command-line developer tools:

```bash
xcode-select --install
```

Install GNU Make if required by the project's Makefile and verify that the compiler supports the required C and C++ standards.

---

## 10. Run the React Prototype

Clone the repository:

```bash
git clone https://github.com/VarunM1234/PBL_1.git
cd PBL_1
```

Install the dependencies:

```bash
npm install
```

Create the environment file.

On Linux or macOS:

```bash
cp .env.example .env
```

On Windows PowerShell:

```powershell
Copy-Item .env.example .env
```

Open `.env` and configure the required variables, including the Gemini key if AI functionality is being tested.

```dotenv
GEMINI_API_KEY=your_gemini_api_key
```

Do not commit the real API key.

Start the development server:

```bash
npm run dev
```

Open the local URL printed by Vite or the Express server in the terminal.

The original project documentation specifies:

```text
http://localhost:3000
```

However, the actual port depends on the current server configuration. Use the URL printed by the running application.

### Verify the npm Scripts

Before using an npm command, inspect the `scripts` section of `package.json`.

```bash
npm run
```

This lists the scripts available in the current repository.

If `npm run dev` is not defined, do not invent a replacement command. Check `package.json` and use the script that the project actually provides.

### Dependency Installation Issues

If installation reports a peer-dependency conflict involving `esbuild`, inspect the dependency versions in `package.json` and `package-lock.json`.

The original project notes that the expected `esbuild` version is `^0.28.0`. Confirm compatibility with the actual installed Vite version before changing dependencies.

Avoid deleting the lockfile or using `--force` as the first troubleshooting step.

---

## 11. Compile and Run the C/C++ Application

The following commands assume the repository contains a Makefile with the corresponding targets.

Open a terminal in the directory containing the Makefile.

### 11.1 Clean Previous Build Files

```bash
make clean
```

This removes generated build artifacts only if the target is defined accordingly.

### 11.2 Compile the Application

```bash
make
```

The build should compile the C modules, compile the C++ modules, and link them into the executable.

### 11.3 Check Available Targets

```bash
make help
```

If the Makefile does not define a `help` target, inspect it directly:

```bash
cat Makefile
```

On Windows PowerShell:

```powershell
Get-Content Makefile
```

### 11.4 Run the Console Application

The documented executable name is:

```bash
./bin/smartroute
```

On Windows, the executable may have the `.exe` extension:

```powershell
.\bin\smartroute.exe
```

The actual executable name and output directory depend on the Makefile.

### 11.5 Run the Web Application

The documented web-server command is:

```bash
./bin/smartroute --serve
```

On Windows, use the generated executable and the same supported argument:

```powershell
.\bin\smartroute.exe --serve
```

If the repository supplies a wrapper script, it may also be used.

Linux or macOS:

```bash
./run-web.sh
```

Windows:

```powershell
.\run-web.cmd
```

Use the wrapper only if it exists in the repository and its configuration matches the current build.

### 11.6 Open the Website

After starting the C++ HTTP server, open the local URL reported in the terminal.

For example, if the server is configured to use port `8080`:

```text
http://localhost:8080
```

The actual port must be confirmed from the server configuration or startup message.

### Important Compilation Note

The C/C++ application and React prototype have different build processes.

Do not run `npm install` inside the C/C++ project directory unless the actual project defines an npm-based task.

If npm reports that `package.json` cannot be found, first verify the current directory:

```powershell
Get-Location
Get-ChildItem
```

Then navigate to the folder containing the intended project's `package.json` or Makefile.

---

## 12. Build the Standalone C++ Version

The C/C++ project may include a single-file build tool, such as `tools/onefile.cpp`, to combine the required C/C++ source into one standalone source file.

This is useful for academic submission, portability, and environments where a single source file is preferred.

### 12.1 Check the Tool

Confirm that the file exists:

```bash
ls tools
```

On Windows PowerShell:

```powershell
Get-ChildItem .\tools
```

### 12.2 Build the Single-File Generator

The exact command depends on how the generator is implemented. If it is a standalone C++ program, a typical compilation command is:

```bash
g++ -std=c++17 tools/onefile.cpp -o onefile
```

On Windows:

```powershell
g++ -std=c++17 tools/onefile.cpp -o onefile.exe
```

These commands are examples and must be checked against the generator's actual dependencies and command-line interface.

### 12.3 Generate the Combined Source

Check the tool's documentation or source code to determine its supported arguments before generating the file.

Do not assume that running `onefile` without arguments produces the correct output.

### 12.4 Compile the Combined Source

If the generator produces a compatible single C++ source file named `smartroute.cpp`, the compilation command would be:

```bash
g++ -std=c++17 -O2 smartroute.cpp -o smartroute
```

On Windows:

```powershell
g++ -std=c++17 -O2 smartroute.cpp -o smartroute.exe
```

Use these commands only when the generated file actually contains all required implementation code and does not depend on separate C objects or additional assets.

If the standalone build embeds the HTML and map assets, verify that the generator includes the latest version of those files.

---

## 13. Data Structures and Algorithms

One of SmartRoute's academic objectives is to demonstrate how data structures and algorithms can be applied to a transportation-management problem.

The C/C++ implementation contains hand-written data structures rather than relying exclusively on standard library containers.

| Data Structure / Algorithm | Application |
|---|---|
| Dynamic Array | Stores collections of buses, routes, and other records |
| Linked List | Supports dynamically connected records |
| Queue | Processes waiting operations in order |
| Stack | Supports undo and last-in-first-out operations |
| Hash Table | Supports key-based lookup |
| Min-Heap | Schedules seat-hold expiry and supports staff assignment |
| Trie | Supports city-prefix autocomplete |
| Bitset | Represents seat states compactly |
| Fenwick Tree | Supports prefix and range-sum calculations for revenue |
| Ring Buffer | Stores bounded sequences of recent events |
| LRU Cache | Demonstrates cache eviction |
| Graph | Represents cities, stops, and route connections |
| Union-Find | Supports disjoint-set operations |
| Sorting Algorithms | Orders bus results and records |
| Searching Algorithms | Finds matching records |
| Dijkstra's Algorithm | Finds a minimum-cost route when non-negative edge weights are used |
| Breadth-First Search | Explores routes in an unweighted graph |
| Greedy Assignment | Selects staff candidates according to implemented assignment rules |
| SHA-256 | Supports the project's educational hashing implementation |
| QR Encoding | Generates QR codes for supported ticket workflows |

### 13.1 Graph Representation

The route network is represented as a graph.

- Vertices represent cities, stops, or route points.
- Edges represent connections between points.
- Edge weights represent the route cost used by the selected algorithm.

Dijkstra's algorithm can find a minimum-weight route when the graph's edge weights are non-negative.

Breadth-first search is useful when the objective is to minimize the number of edges in an unweighted graph.

These algorithms do not automatically guarantee the fastest real-world journey unless the edge weights accurately represent the relevant travel-time information.

### 13.2 Seat Availability

A bitset can represent seat states compactly.

For example, each bit can represent whether a seat is available, selected, held, or occupied according to the implementation's state encoding.

The exact meaning of each bit must be taken from the actual C source.

### 13.3 Seat-Hold Expiry

A min-heap can store holds ordered by their expiration time.

When the earliest expiration is reached, the application can process expired holds before accepting a new booking.

A local demonstration implementation does not automatically provide distributed locking across multiple servers or browser sessions.

### 13.4 Prefix Search

A trie supports prefix-based city lookup.

For example, typing `Del` can return city names that begin with that prefix, depending on the configured catalogue and matching rules.

### 13.5 Revenue Calculations

A Fenwick tree supports prefix sums and range-sum queries.

It can be used to calculate the sum of revenue entries over a selected index range without recomputing every earlier entry.

The revenue shown by SmartRoute is demonstration data and should not be interpreted as actual operator revenue.

---

## 14. Booking Workflow

The booking process follows these conceptual steps.

```text
Passenger Opens Application
            |
            v
      Search Bus Route
            |
            v
       View Bus Results
            |
            v
       Select a Service
            |
            v
        Select Seats
            |
            v
       Hold Seat(s)
            |
            v
    Enter Passenger Details
            |
            v
       Apply Coupon
            |
            v
     Review Total Fare
            |
            v
     Simulated Checkout
            |
            v
     Create Demo Booking
            |
            v
      Generate Ticket
            |
            v
      Track the Journey
```

### 14.1 Seat Selection

The passenger selects one or more seats from the displayed layout.

The application checks the current seat state and applies the supported booking rules.

In the React prototype, seat availability and holds are browser-side demonstration state.

The C/C++ implementation has its own booking and persistence logic. Its exact concurrency guarantees depend on the implemented storage and locking mechanisms.

### 14.2 Fare Calculation

The final amount can depend on:

- Base fare.
- Selected seats.
- Coupon discount.
- Applicable taxes.
- Optional insurance.
- Full-payment or deposit mode.

The amount displayed is a demonstration amount and does not represent a real transaction.

### 14.3 Booking Confirmation

After the simulated checkout succeeds, the application displays the booking reference and ticket information.

The React prototype does not persist the current booking flow in a production database.

The C/C++ implementation uses its own application storage mechanism for supported records.

### 14.4 Cancellation and Refund

The application can demonstrate cancellation and refund calculations.

The refund preview is an application rule, not a real refund transaction. No payment provider is contacted by the simulated checkout.

---

## 15. Route Planning and Live Map

### 15.1 Original React Map

The React prototype uses Leaflet to render a map with OpenStreetMap tiles.

The documented route focuses on Delhi to Jaipur along NH-48.

The bus marker moves through the simulated journey using the application's tracking controls.

### 15.2 C/C++ Map

The C/C++ implementation serves an HTML tracking interface from the C++ HTTP server.

The browser can use Leaflet, external map tiles, and a public OSRM routing service to display geographic information and route geometry.

### 15.3 Tracking Controls

The demonstration tracking workflow may include:

- Advance.
- Traffic Jam.
- Reset.
- Simulated bus movement.
- Route stops.
- Journey progress.
- Estimated arrival.
- Event notifications.

The controls change the demonstration state; they do not control a physical bus.

### 15.4 GPS Limitation

The current project does not receive verified GPS coordinates from a real bus operating on the road.

The simulated position is not proof of a vehicle's physical location.

### 15.5 Traffic Limitation

Traffic jams and delays are simulated. A public routing service may return route geometry, but this does not mean the application has a verified, continuously updated live traffic feed.

### 15.6 ETA Limitation

Estimated arrival times are based on the data and calculations available to the current implementation.

They should not be treated as reliable real-world arrival predictions until validated against actual journeys and live vehicle data.

### 15.7 Map Availability

Online map tiles and routing requests require internet access.

The project may use a generated or schematic fallback when external services are unavailable, depending on the implementation.

Public map services have their own usage policies, availability constraints, and rate limits.

---

## 16. Notifications and Delay Management

### 16.1 Simulated Journey Alerts

The React prototype generates alerts when the simulated bus reaches configured route events.

These can include:

- Approaching a toll plaza.
- Approaching the midway halt.
- Arriving at the destination.

The browser can display a toast, update the notification bell, and send a browser notification after permission is granted.

Browser notifications require user permission and work only while the browser is open in the current prototype.

Closing the browser does not provide a guaranteed background notification service.

### 16.2 Delay Reporting

The C/C++ implementation demonstrates driver-reported delays.

Possible reasons include:

- Heavy traffic.
- Accident ahead.
- Road closure.
- Vehicle breakdown.
- Extended halt.

The available reasons depend on the implementation.

### 16.3 Planned Notifications

A production version could introduce:

- Server-side event storage.
- Web push notifications.
- Firebase Cloud Messaging.
- Email or SMS notifications.
- A service worker.
- Notifications while the browser is closed.

These capabilities must be implemented and tested before being described as active features.

---

## 17. Data Storage

### 17.1 React Prototype

The current React booking interface uses browser-side state for its demonstration workflow.

The repository contains Firebase helpers and Firestore rules, but the running booking interface does not currently use Firestore as its active database.

Consequently, the existence of Firebase files should not be interpreted as proof of active database persistence or authentication.

### 17.2 C/C++ Implementation

The C/C++ application uses CSV files for sample catalogue data and an application storage layer for supported persistent records.

The data directory may contain:

```text
data/
├── cities.csv
├── points.csv
├── routes.csv
├── buses.csv
├── schedules.csv
├── staff.csv
└── save/
```

The CSV files provide sample records used by the application.

The save directory is intended for local application state, depending on the configured storage implementation.

### 17.3 Data Integrity

Before editing sample data:

1. Make a backup of the original files.
2. Preserve the existing CSV headers.
3. Use the expected delimiter.
4. Check IDs and references between related records.
5. Ensure that numeric fields use the expected format.
6. Restart the application if it loads catalogue data only at startup.

Do not assume that manually editing a CSV file updates an already running process.

### 17.4 Production Database

The documented project does not provide a configured production database for the current React booking flow.

A production implementation would require a database, migrations, transaction handling, shared seat locks, backups, access control, and recovery procedures.

---

## 18. Testing and Evaluation

Testing should distinguish implemented functionality from planned or unverified functionality.

### 18.1 Suggested Functional Tests

| Test | Expected Result |
|---|---|
| Search for a valid route | Matching sample services are displayed |
| Search with an invalid route | No matching service or a validation message is displayed |
| Apply a valid coupon | The supported discount is calculated |
| Apply an invalid coupon | The application rejects the coupon |
| Select an available seat | The seat is marked as selected |
| Select an unavailable seat | The application prevents selection |
| Complete simulated checkout | A demonstration booking is created |
| Cancel a supported booking | The application calculates the applicable refund preview |
| Advance simulated tracking | The bus marker or journey state changes |
| Reset the simulated trip | The tracking state returns to its initial state |
| Trigger a configured route event | The application displays the corresponding alert |
| Submit an invalid location | The application rejects invalid coordinates |
| Assign conflicting staff | The application rejects supported conflicting assignments |
| Start the C++ web server | The browser interface becomes available |
| Load a missing CSV file | The application should report a useful error where implemented |

These are test cases to run, not claims that every test has already passed.

### 18.2 C/C++ Tests

If the repository includes a Makefile target for tests, inspect the available targets:

```bash
make help
```

Or inspect the Makefile directly:

```bash
cat Makefile
```

The documented C/C++ repository may include tests such as:

```text
tests/test_core.c
tests/test_core2.c
tests/test_app.cpp
tests/test_ops.cpp
tests/bench.cpp
```

Use the actual target defined in the repository to build and run them.

### 18.3 Planned Evaluation

| Measure | Evaluation Method |
|---|---|
| ETA Error | Compare predicted arrival times with actual recorded arrival times |
| Tracking Delay | Measure the time between a location update and its display |
| Task Completion Time | Measure how long users need to find a bus and complete a demonstration booking |
| Booking Correctness | Verify that two concurrent requests cannot both reserve the same seat |
| Route Accuracy | Compare the selected route with the expected route |
| Notification Accuracy | Verify that each configured route event triggers the correct alert |
| Persistence | Restart the application and check whether supported records are retained |

**Evaluation status:** These are proposed evaluation methods. Actual results must be collected and documented after the tests have been performed.

---

## 19. Security and Privacy

### 19.1 Environment Variables

Secrets should be stored in `.env` rather than committed to source control.

The `.env.example` file should contain placeholder values only.

Never commit:

- Gemini API keys.
- Authentication secrets.
- Production credentials.
- Private configuration files.
- Real passenger data.

### 19.2 Gemini API

The original React prototype calls Gemini through server-side endpoints.

The API key must remain on the server and should not be embedded in frontend JavaScript.

The documented prototype has a known security gap: its `/api/ai/*` endpoints do not have a sign-in check or rate limiting. Anyone who can reach those endpoints may be able to consume the configured API quota.

Do not expose these endpoints publicly without appropriate authentication, authorization, input validation, and rate limiting.

### 19.3 Firebase

The repository contains Firestore security rules intended to deny access by default and restrict access to supported owner-scoped records.

The running booking interface does not currently use Firestore as its active database. The rules should be tested against the actual Firebase project before deployment.

### 19.4 Passwords and Authentication

The C/C++ implementation includes an educational SHA-256 implementation.

A general-purpose hash function alone is not an appropriate password-storage scheme. Production authentication should use a dedicated password-hashing algorithm such as Argon2id or an appropriately configured bcrypt implementation, with secure session management.

### 19.5 Local HTTP Server

The demonstration C++ HTTP server is intended for local academic use.

Before exposing it to a public network, implement and verify:

- Authentication.
- Role-based authorization.
- Request validation.
- Rate limiting.
- Secure session management.
- HTTPS through a properly configured deployment.
- Protection against unauthorized file access.
- Secure error handling.
- Audit logging for sensitive operations.

### 19.6 Passenger Privacy

Use fictional passenger information during demonstrations.

Do not upload actual passenger manifests, phone numbers, or personal information to a public repository.

---

## 20. Known Limitations

### 20.1 Simulated GPS

The bus position and movement are simulated. The application does not currently receive verified GPS data from a real bus.

### 20.2 Simulated Traffic

Traffic jams, delays, and travel-time changes are demonstration behavior. The project does not currently provide a verified continuous live traffic feed.

### 20.3 Route Geometry

The original React prototype draws straight lines between configured stops. These lines do not represent accurate road geometry.

The C/C++ version may request road geometry from a public routing service, but route quality depends on service availability and the data returned.

### 20.4 Seat Availability

The React prototype uses sample seat data and browser-side holds. It does not have a shared seat-locking mechanism across users.

### 20.5 Booking Persistence

The React prototype does not save the current booking workflow to a production database.

The C/C++ version uses local application persistence for supported records, which does not automatically provide multi-server consistency or distributed transactions.

### 20.6 Real Payments

Payments are simulated. No real money is collected and no payment provider processes the documented demo checkout.

### 20.7 AI Integration

The React prototype's Gemini endpoints require a valid API key. Their functionality has not been verified merely by the presence of `server.ts`.

The C/C++ Trip Assistant is rule-based rather than generative AI.

### 20.8 External Map Services

The public map tile and routing services may be unavailable, slow, or subject to usage restrictions.

Public endpoints should not be assumed suitable for a high-traffic production deployment.

### 20.9 Background Notifications

The current browser notification workflow depends on the browser being open. Notifications while the browser is closed require additional infrastructure.

### 20.10 Testing

Do not describe the project as fully tested unless the relevant automated and manual tests have actually been executed and their results recorded.

---

## 21. Troubleshooting

### 21.1 `npm install` Reports `ENOENT`

Example:

```text
npm error code ENOENT
npm error syscall open
npm error path ...\package.json
```

This usually indicates that npm cannot find the expected `package.json` in the current directory.

Check the current directory:

```powershell
Get-Location
Get-ChildItem
```

Navigate to the React project directory containing `package.json`:

```powershell
cd C:\path\to\PBL_1
npm install
```

Do not run `npm install` in a folder containing only the C/C++ source files unless that folder intentionally includes an npm package.

### 21.2 `make` Is Not Recognized

Verify that GNU Make is installed:

```bash
make --version
```

On Windows, open the MSYS2 UCRT64 terminal if that is the configured build environment.

### 21.3 `gcc` or `g++` Is Not Recognized

Verify the compiler installation:

```bash
gcc --version
g++ --version
```

On Windows, check that the appropriate MSYS2 compiler environment is being used.

### 21.4 The Website Does Not Open

1. Confirm that the server started successfully.
2. Check the URL and port printed in the terminal.
3. Confirm that the port is not already in use.
4. Inspect the terminal for startup errors.
5. Check whether the server is configured to serve the expected HTML directory.

### 21.5 The Map Is Blank

1. Check the internet connection.
2. Inspect the browser developer console.
3. Verify that the map library loaded.
4. Check whether the map tile provider is responding.
5. Check whether the configured map container has a valid height.
6. Check whether the public routing service is reachable.

### 21.6 The Gemini Endpoint Fails

1. Confirm that `.env` exists.
2. Verify the `GEMINI_API_KEY` variable.
3. Restart the server after changing environment variables.
4. Check the server logs.
5. Confirm that the API key is valid and has the required permissions.

### 21.7 The C/C++ Application Cannot Find Data Files

The application may use paths relative to its current working directory.

Start it from the project root unless the implementation explicitly supports another working directory.

Check that the required CSV files exist:

```bash
ls data
```

On Windows PowerShell:

```powershell
Get-ChildItem .\data
```

### 21.8 Booking Changes Disappear

In the React prototype, booking information is stored in browser-side state, so a reload may discard the current demonstration data.

In the C/C++ implementation, check the configured save directory and storage error messages. Not every screen or record is necessarily persisted.

---

## 22. Future Enhancements

The following improvements are proposed and should not be considered completed features.

### 22.1 Real-Time GPS

- Integrate a driver mobile interface with device GPS.
- Validate location updates on the server.
- Broadcast location changes to passenger clients.
- Store trip history.
- Display the actual vehicle position.
- Add configurable location-update intervals.

### 22.2 Traffic-Aware Routing

- Integrate a supported routing API.
- Retrieve road geometry and traffic-aware durations.
- Compare alternative routes.
- Recalculate ETA when new data arrives.
- Display delays and detours.
- Cache routing results according to provider policies.

### 22.3 Shared Booking Database

- Introduce a production database.
- Implement schema migrations.
- Add transaction-safe seat reservations.
- Prevent duplicate bookings across concurrent users.
- Add automated cleanup for expired holds.
- Implement backup and recovery procedures.

### 22.4 Secure Authentication

- Add verified passenger, driver, operator, and administrator accounts.
- Implement role-based authorization.
- Use secure password hashing.
- Protect sensitive API endpoints.
- Add session expiry and revocation.

### 22.5 Payment Gateway

- Integrate a supported payment provider in test mode first.
- Verify payment signatures on the server.
- Implement idempotent payment processing.
- Add payment reconciliation and refund handling.
- Keep all secret keys on the server.

### 22.6 Notifications

- Add server-side notification storage.
- Implement web push or Firebase Cloud Messaging.
- Support notifications while the browser is closed.
- Add configurable email or SMS alerts.
- Prevent duplicate notification delivery.

### 22.7 Testing and Deployment

- Add automated unit and integration tests.
- Test concurrent booking requests.
- Add load and security testing.
- Add deployment configuration.
- Implement health checks and structured logs.
- Document backup, recovery, and monitoring procedures.

---

## 23. Team Contributions

| Name | Roll Number | Role |
|---|---|---|
| Varun Maheshwary | 2510360125 | Team Lead, Architecture, Tracking |
| Trisha Gupta | 2512510016 | Passenger Module and UI |
| Riya Gupta | 2510012490 | Database and Testing |
| Harshita | 2510012152 | Documentation and Driver Module |

---

## 24. Academic Context

SmartRoute was developed as part of the Project-Based Learning (PBL) curriculum at Graphic Era (Deemed to be University), Dehradun, during the 2026–2027 academic session.

The project demonstrates the practical application of:

- C and C++ programming.
- Data structures and algorithms.
- Graph-based route planning.
- Application architecture.
- Web development.
- File handling and local persistence.
- Simulation and testing.
- Software documentation.
- Team-based development.

---

## 25. License and Third-Party Notices

No project-wide license has been specified.

The C/C++ implementation documents the use of Leaflet 1.9.4, which is distributed under the BSD-2-Clause license. Refer to the included third-party license file and the applicable Leaflet license terms.

OpenStreetMap data and map services are subject to their respective attribution requirements and usage policies. Other map tile providers and routing services may impose additional conditions.

Before public deployment:

1. Specify an appropriate license for the project's own source code.
2. Preserve required third-party notices.
3. Confirm map data attribution requirements.
4. Review third-party service usage policies.
5. Remove secrets and private data from the repository.

---

## Acknowledgements

The SmartRoute project brings together programming, data structures, algorithms, route planning, web development, and software documentation in an academic transportation-management prototype.

The project is intended to demonstrate how these concepts can be combined to build a more comprehensive bus-booking and tracking application.

---

**SmartRoute — Search. Book. Track.**

*Academic prototype for bus booking, fleet management, and simulated live tracking.*
