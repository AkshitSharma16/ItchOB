#include <iostream>
#include <fstream> //provides ifstream datatype
#include <cstdint> //provides fixed byte datatypes
#include <string>
#include <cstring>
#include <chrono> //for time analysis

#pragma pack(push,1) //ensures struct is not padded as itch data is continuous
struct TradeMessage{
    char messageType;
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t timestampBytes[6]; //array of 6 individual bytes as uint48_t doesnt exist
    uint64_t orderReference;
    char buySellIndicator;
    uint32_t shares;
    char stock[8];
    uint32_t price;
};
#pragma pack(pop)
uint16_t swap16(uint16_t val){return __builtin_bswap16(val);}//to swap little endian format on normal computers to big endian and vice versa
uint64_t swap64(uint64_t val){return __builtin_bswap64(val);}
uint32_t swap32(uint32_t val){return __builtin_bswap32(val);} 

uint64_t parseTimestamp(const uint8_t *bytes){
    uint64_t nanoseconds = 0;
    nanoseconds |= static_cast<uint64_t>(bytes[0]) << 40; //reverse of generaing using bitwise OR this time to keep all 1s
    nanoseconds |= static_cast<uint64_t>(bytes[1]) << 32;
    nanoseconds |= static_cast<uint64_t>(bytes[2]) << 24;
    nanoseconds |= static_cast<uint64_t>(bytes[3]) << 16;
    nanoseconds |= static_cast<uint64_t>(bytes[4]) << 8;
    nanoseconds |= static_cast<uint64_t>(bytes[5]);
    return nanoseconds;
}

int main(){
    std::ifstream inFile("mock_market_data.bin",std::ios::binary);
    if(!inFile){
        std::cerr << "Error reading file!" << std::endl;
        return 1;
    }

    TradeMessage msg;
    int count = 0;

    auto start = std::chrono::high_resolution_clock::now();

    while (inFile.read(reinterpret_cast<char*>(&msg), sizeof(TradeMessage))) {
        count++;
        char type = msg.messageType;
        uint16_t tracking = swap16(msg.trackingNumber);
        uint16_t locate = swap16(msg.stockLocate);
        uint64_t timestamp = parseTimestamp(msg.timestampBytes);
        uint64_t ref = swap64(msg.orderReference);
        char buysell = msg.buySellIndicator;
        uint32_t shares = swap32(msg.shares);
        std::string stock(msg.stock,8);
        double price = swap32(msg.price)/10000.0;

        //uncomment this to print the first 10 messages
        /*if(count <= 10){
            std::printf("%s |     %c |  %7u | $%10.4f | %.6f\n",
                stock.c_str(),buysell,shares,price,timestamp/1'000'000'000.0
            );
        }*/

    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "\nBENCHMARK RESULTS" << std::endl;
    std::cout << "Successfully parsed: " << count << " messages" << std::endl;
    std::cout << "Total Elapsed Time: " << duration << " microseconds" << std::endl;
    if (count > 0) {
        std::cout << "Average Latency: " << static_cast<double>(duration) * 1000.0 / count 
                  << " nanoseconds per message" << std::endl;
    }
    inFile.close();

    return 0;
}