# NASDAQ ITCH 5.0 Parser & Order Book Engine
A low latency Linux based C++ parser for binary NASDAQ ITCH 5.0 messages, capable of maintaining real-time order book state depth.
This software is designed to parse binary NASDAQ 5.0 ITCH data, construct the Limit Order Book for a specific target asset (here, VOD) and write it to a CSV file. It utilises a 2 thread Producer-Consumer architecture as follows-
1.	The Producer Thread (Main thread): Memory-maps the file and parses the messages, then pushes event structs into a lock free (SPSC Ring Buffer) queue according to the message type.
2.	The Consumer Thread (orderBookConsumerThread): Does most of the heavy lifting, which includes maintaining active order states and sorted price levels, computing best bid/ask prices and writing them to the csv file.

Note: For an in-depth technical analysis, refer to the project [Documentation](./Documentation_NASDAQ_ITCH_Parser.pdf).

## Preliminary Files:
- randomGenerator.cpp: Generates 10000 sample order Add messages in the binary file (mock_market_data.bin) for small scale testing.
- sampleParser.cpp: Simply parses these sample messages and outputs the average latency per message.
- simpleParser.cpp: Parses the actual NASDAQ ITCH 5.0 data and outputs the average latency per message.
- fastParser.cpp: Uses linux low level operations to reduce latency and parse messages quicker.

## Final File:
- parserandOB.cpp: Uses the 2 thread architecture to parse the Itch data and maintains a real time order book in the form of a csv file (orderbook_snapshots.csv).

## Build and Run:
- Ensure you have a modern C++ compiler supporting C++17 installed in a Linux/Unix environment, and the ITCH data file is in the same project directory.
- Compilation: g++ -O2 -std=c++17 -pthread parserandOB.cpp -o parserandOB
- Running: ./parserandOB

## Latency results:
- simpleParser.cpp: Average latency per message ~ 616.16 nanoseconds/message.
- fastParser.cpp: Average latency per message ~ 35.25 nanoseconds/message.
- Decrease in latency is about 94.28%.
