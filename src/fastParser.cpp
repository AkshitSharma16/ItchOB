#include <iostream>
#include <cstdint> //provides fixed byte datatypes
#include <string>
#include <cstring>
#include <chrono> //for time analysis

//for low level linux specific operations
#include <sys/mman.h> //provides low level memory management- mmap, munmap and madvise
#include <sys/stat.h> //provides fstat and struct stat to find file size
#include <fcntl.h> //provides low level file open function
#include <unistd.h> //provides linux specific close


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
inline uint16_t swap16(uint16_t val){return __builtin_bswap16(val);}//to swap little endian format on normal computers to big endian and vice versa
inline uint64_t swap64(uint64_t val){return __builtin_bswap64(val);}
inline uint32_t swap32(uint32_t val){return __builtin_bswap32(val);} 

inline uint64_t parseTimestamp(const uint8_t *bytes){
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
    const char* filepath = "real_data.NASDAQ_ITCH50";
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        std::cerr << "Error opening file!" << std::endl;
        return 1;
    }
    //to get file size
    struct stat sb;
    if(fstat(fd,&sb) == -1){
        std::cerr << "Error getting file size!" << std::endl;
        close(fd);
        return 1;
    }
    size_t fileSize = sb.st_size;

    //memory-map the file
    const uint8_t* fileBuffer = reinterpret_cast<const uint8_t*>(
        mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0)
    );
    if (fileBuffer == MAP_FAILED) {
        std::cerr << "Memory mapping failed!" << std::endl;
        close(fd);
        return 1;
    }

    madvise((void*)fileBuffer, fileSize, MADV_SEQUENTIAL); //tell kernel we will read data sequentially

    uint64_t totalMessages = 0;
    uint64_t orderMessages = 0;
    const uint8_t* ptr = fileBuffer;
    const uint8_t* endPtr = fileBuffer + fileSize;

    auto start = std::chrono::high_resolution_clock::now();
    while(ptr< endPtr){
        uint16_t msgLength = swap16(*reinterpret_cast<const uint16_t*>(ptr));
        ptr+=2; //skip the 2 byte length we read
        totalMessages++;
        char msgType = *reinterpret_cast<const char*>(ptr);
        if(msgType == 'A'){
            orderMessages++;
            const TradeMessage* msg = reinterpret_cast<const TradeMessage*>(ptr);

            uint16_t tracking = swap16(msg->trackingNumber);
            uint16_t locate = swap16(msg->stockLocate);
            uint64_t timestamp = parseTimestamp(msg->timestampBytes);
            uint64_t ref = swap64(msg->orderReference);
            char buysell = msg->buySellIndicator;
            uint32_t shares = swap32(msg->shares);
            std::string stock(msg->stock, 8);
            double price = swap32(msg->price) / 10000.0;

            //uncomment to print the first 10 messages
            /*if(orderMessages <= 10){
                std::printf("%s |     %c |  %7u | $%10.4f | %.6f\n",
                    stock.c_str(), buysell, shares, price, timestamp / 1'000'000'000.0
                );
            }*/
        }
        ptr += msgLength;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "\nBENCHMARK RESULTS" << std::endl;
    std::cout << "Successfully parsed: " << totalMessages << " messages" << std::endl;
    std::cout << "Successfully parsed: " << orderMessages << " Add messages" << std::endl;
    std::cout << "Total Elapsed Time: " << duration << " microseconds" << std::endl;
    if (totalMessages > 0) {
        std::cout << "Average Latency: " << static_cast<double>(duration) * 1000.0 / totalMessages 
                  << " nanoseconds per message" << std::endl;
    }

    munmap((void*)fileBuffer, fileSize);
    close(fd);

    return 0;
}