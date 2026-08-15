#include <iostream>
#include <fstream> //provides ofstream datatype
#include <cstdint> //provides fixed byte datatypes
#include <string>
#include <cstring> //provides memcpy
#include <random>

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

void packTimestamp(uint64_t nanoseconds, uint8_t *target){
    target[0] = static_cast<uint8_t>((nanoseconds >> 40) & 0xFF); // bitwise shift to get one byte (8 bits) to the end and bitwise AND with 255 to get rid of trailing 0s 
    target[1] = static_cast<uint8_t>((nanoseconds >> 32) & 0xFF);
    target[2] = static_cast<uint8_t>((nanoseconds >> 24) & 0xFF);
    target[3] = static_cast<uint8_t>((nanoseconds >> 16) & 0xFF);
    target[4] = static_cast<uint8_t>((nanoseconds >> 8)  & 0xFF);
    target[5] = static_cast<uint8_t>(nanoseconds & 0xFF);
}


int main(){
    std::ofstream outFile("mock_market_data.bin",std::ios::binary);
    if(!outFile){
        std::cerr << "Error creating output file!" << std::endl;
        return 1;
    }
    std::random_device rd; //random number generator using hardware entropy
    std::mt19937 gen(rd()); //fast engine that geneates 4 byte random numbers based on seed
    std::uniform_int_distribution<uint32_t> shareDist(10,500);
    std::uniform_int_distribution<uint32_t> priceDist(1000000,2000000);
    std::string tickers[] = {"AAPL    ", "NVDA    ", "MSFT    "};


    for(int i = 0; i<10000;i++){
        TradeMessage msg;

        msg.messageType = 'A';
        msg.stockLocate = 0;
        msg.trackingNumber = 0;

        uint64_t mockNanoseconds = 34200000000000ULL + (i * 1000000ULL);
        packTimestamp(mockNanoseconds, msg.timestampBytes);

        msg.orderReference = swap64(10000000 + i);
        msg.buySellIndicator = (i%2==0? 'B':'S');
        msg.shares = swap32(shareDist(gen));
        msg.price = swap32(priceDist(gen));
        std::string selectedTicker = tickers[i%3];
        std::memcpy(msg.stock, selectedTicker.c_str(),8);

        outFile.write(reinterpret_cast<const char*>(&msg),sizeof(TradeMessage));

    }

    outFile.close();
    std::cout << "File creation successfull" << std::endl;


    return 0;
}