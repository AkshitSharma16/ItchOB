#include <iostream>
#include <chrono>
#include <cstdint>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <new>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#pragma pack(push, 1)
struct AddOrder {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t orderReference; char buySellIndicator; uint32_t shares; char stock[8]; uint32_t price;
};
struct OrderExecuted {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t orderReference; uint32_t shares; uint64_t matchId;
};
struct OrderExecutedWithPrice {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t orderReference; uint32_t shares; uint64_t matchId; uint8_t printable; uint32_t price;
};
struct OrderCancel {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t orderReference; uint32_t shares;
};
struct OrderDelete {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t orderReference;
};
struct OrderReplace {
    char messageType; uint16_t stockLocate; uint16_t trackingNumber; uint8_t timestamp[6];
    uint64_t originalOrderReference; uint64_t newOrderReference; uint32_t shares; uint32_t price;
};
#pragma pack(pop)

struct OrderEvent {
    char type;
    char side;
    uint64_t orderId;
    uint64_t newOrderId;
    uint32_t shares;
    uint32_t price;
};

struct LocalOrder {
    uint32_t price;
    uint32_t shares;
    char side;
};

template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
    SPSCRingBuffer() : head(0), tail(0) {}
    bool enqueue(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t current_head = head.load(std::memory_order_acquire);
        if ((current_tail - current_head) == Capacity) return false;
        buffer[current_tail & (Capacity - 1)] = item;
        tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }
    bool dequeue(T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t current_tail = tail.load(std::memory_order_acquire);
        if (current_head == current_tail) return false;
        item = buffer[current_head & (Capacity - 1)];
        head.store(current_head + 1, std::memory_order_release);
        return true;
    }
private:
    T buffer[Capacity];
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};

SPSCRingBuffer<OrderEvent, 262144> ringBuffer;
std::atomic<bool> parserFinished(false);

static const char* CSV_OUTPUT_PATH = "orderbook_snapshots.csv";

inline uint16_t swap16(uint16_t val) { return __builtin_bswap16(val); }
inline uint32_t swap32(uint32_t val) { return __builtin_bswap32(val); }
inline uint64_t swap64(uint64_t val) { return __builtin_bswap64(val); }

void writeOrderBook(FILE* csv, uint64_t tick, const std::map<uint32_t, uint32_t, std::greater<uint32_t>>& bids, const std::map<uint32_t, uint32_t, std::less<uint32_t>>& asks) {
    auto bidIt = bids.begin();
    auto askIt = asks.begin();

    bool hasBid = bidIt != bids.end();
    bool hasAsk = askIt != asks.end();

    double bestBidPrice = hasBid ? bidIt->first / 10000.0 : 0.0;
    double bestAskPrice = hasAsk ? askIt->first / 10000.0 : 0.0;
    bool crossed = hasBid && hasAsk && (bidIt->first >= askIt->first);

    std::fprintf(csv, "%llu,", (unsigned long long)tick);

    if (hasBid) std::fprintf(csv, "%.4f,%u,", bestBidPrice, bidIt->second);
    else std::fprintf(csv, ",,");

    if (hasAsk) std::fprintf(csv, "%.4f,%u,", bestAskPrice, askIt->second);
    else std::fprintf(csv, ",,");

    if (hasBid && hasAsk) std::fprintf(csv, "%.4f,", bestAskPrice - bestBidPrice);
    else std::fprintf(csv, ",");

    std::fprintf(csv, "%d", crossed ? 1 : 0);

    for (int i = 0; i < 3; ++i) {
        std::fprintf(csv, ",");
        if (bidIt != bids.end()) {
            std::fprintf(csv, "%.4f,%u", bidIt->first / 10000.0, bidIt->second);
            ++bidIt;
        } else {
            std::fprintf(csv, ",");
        }
    }
    for (int i = 0; i < 3; ++i) {
        std::fprintf(csv, ",");
        if (askIt != asks.end()) {
            std::fprintf(csv, "%.4f,%u", askIt->first / 10000.0, askIt->second);
            ++askIt;
        } else {
            std::fprintf(csv, ",");
        }
    }

    std::fprintf(csv, "\n");
}

void orderBookConsumerThread() {
    std::unordered_map<uint64_t, LocalOrder> activeOrders;
    std::map<uint32_t, uint32_t, std::greater<uint32_t>> bids;
    std::map<uint32_t, uint32_t, std::less<uint32_t>> asks; 

    FILE* csv = std::fopen(CSV_OUTPUT_PATH, "w");
    if (!csv) return;

    static char csvBuf[1 << 20];
    std::setvbuf(csv, csvBuf, _IOFBF, sizeof(csvBuf));
    std::fprintf(csv,
        "tick,best_bid_price,best_bid_qty,best_ask_price,best_ask_qty,spread,crossed,"
        "bid1_price,bid1_qty,bid2_price,bid2_qty,bid3_price,bid3_qty,"
        "ask1_price,ask1_qty,ask2_price,ask2_qty,ask3_price,ask3_qty\n");

    OrderEvent ev;
    uint64_t tickCounter = 0;

    while (true) {
        if (ringBuffer.dequeue(ev)) {
            tickCounter++;

            if (ev.type == 'A') {
                activeOrders[ev.orderId] = {ev.price, ev.shares, ev.side};
                if (ev.side == 'B') bids[ev.price] += ev.shares;
                else asks[ev.price] += ev.shares;
            }
            else if (ev.type == 'U') {
                auto it = activeOrders.find(ev.orderId);
                if (it != activeOrders.end()) {
                    uint32_t oldPrice = it->second.price;
                    uint32_t oldShares = it->second.shares;
                    char side = it->second.side;

                    if (side == 'B') {
                        bids[oldPrice] -= oldShares;
                        if (bids[oldPrice] == 0) bids.erase(oldPrice);
                    } else {
                        asks[oldPrice] -= oldShares;
                        if (asks[oldPrice] == 0) asks.erase(oldPrice);
                    }
                    activeOrders.erase(it);

                    activeOrders[ev.newOrderId] = {ev.price, ev.shares, side};
                    if (side == 'B') bids[ev.price] += ev.shares;
                    else asks[ev.price] += ev.shares;
                }
            }
            else {
                auto it = activeOrders.find(ev.orderId);
                if (it != activeOrders.end()) {
                    uint32_t price = it->second.price;
                    char side = it->second.side;
                    uint32_t deltaShares = (ev.type == 'D') ? it->second.shares : std::min(ev.shares, it->second.shares);

                    if (side == 'B') {
                        bids[price] -= deltaShares;
                        if (bids[price] == 0) bids.erase(price);
                    } else {
                        asks[price] -= deltaShares;
                        if (asks[price] == 0) asks.erase(price);
                    }

                    it->second.shares -= deltaShares;
                    if (it->second.shares == 0 || ev.type == 'D') {
                        activeOrders.erase(it);
                    }
                }
            }

            writeOrderBook(csv, tickCounter, bids, asks);
        } else {
            if (parserFinished.load(std::memory_order_relaxed)) break;
            std::this_thread::yield();
        }
    }

    std::fflush(csv);
    std::fclose(csv);
}

int main() {
    const char* filepath = "real_data.NASDAQ_ITCH50";
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return 1;

    struct stat sb;
    fstat(fd, &sb);
    size_t fileSize = sb.st_size;

    const uint8_t* fileBuffer = reinterpret_cast<const uint8_t*>(
        mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0)
    );
    madvise((void*)fileBuffer, fileSize, MADV_SEQUENTIAL);

    std::thread consumer(orderBookConsumerThread);
    uint16_t vodStockLocate = 0xFFFF;

    const uint8_t* ptr = fileBuffer;
    const uint8_t* endPtr = fileBuffer + fileSize;

    while (ptr < endPtr) {
        if (ptr + 2 > endPtr) break;

        uint16_t msgLength = swap16(*reinterpret_cast<const uint16_t*>(ptr));
        ptr += 2;

        if (ptr + msgLength > endPtr) break;

        char msgType = *reinterpret_cast<const char*>(ptr);
        uint16_t msgLocate = swap16(*reinterpret_cast<const uint16_t*>(ptr + 1));

        if (vodStockLocate != 0xFFFF && msgLocate != vodStockLocate) {
            ptr += msgLength;
            continue;
        }

        if (msgType == 'A') {
            const auto* msg = reinterpret_cast<const AddOrder*>(ptr);
            if (std::memcmp(msg->stock, "VOD     ", 8) == 0) {
                if (vodStockLocate == 0xFFFF) vodStockLocate = msgLocate;
                uint64_t id = swap64(msg->orderReference);

                OrderEvent ev{'A', msg->buySellIndicator, id, 0, swap32(msg->shares), swap32(msg->price)};
                while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
            }
        }
        else if (msgType == 'U') {
            const auto* msg = reinterpret_cast<const OrderReplace*>(ptr);
            uint64_t origId = swap64(msg->originalOrderReference);
            uint64_t newId = swap64(msg->newOrderReference);

            OrderEvent ev{'U', ' ', origId, newId, swap32(msg->shares), swap32(msg->price)};
            while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
        }
        else if (msgType == 'E') {
            const auto* msg = reinterpret_cast<const OrderExecuted*>(ptr);
            uint64_t id = swap64(msg->orderReference);

            OrderEvent ev{'E', ' ', id, 0, swap32(msg->shares), 0};
            while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
        }
        else if (msgType == 'C') {
            const auto* msg = reinterpret_cast<const OrderExecutedWithPrice*>(ptr);
            uint64_t id = swap64(msg->orderReference);

            OrderEvent ev{'E', ' ', id, 0, swap32(msg->shares), 0};
            while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
        }
        else if (msgType == 'X') {
            const auto* msg = reinterpret_cast<const OrderCancel*>(ptr);
            uint64_t id = swap64(msg->orderReference);

            OrderEvent ev{'X', ' ', id, 0, swap32(msg->shares), 0};
            while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
        }
        else if (msgType == 'D') {
            const auto* msg = reinterpret_cast<const OrderDelete*>(ptr);
            uint64_t id = swap64(msg->orderReference);

            OrderEvent ev{'D', ' ', id, 0, 0, 0};
            while (!ringBuffer.enqueue(ev)) std::this_thread::yield();
        }

        ptr += msgLength;
    }

    parserFinished.store(true, std::memory_order_release);
    consumer.join();

    munmap((void*)fileBuffer, fileSize);
    close(fd);
    return 0;
}