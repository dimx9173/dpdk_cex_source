#ifndef _PARSER_H_
#define _PARSER_H_

#include <rte_mbuf.h>
#include "protocol.h" // Include our protocol definitions

/**
 * @brief Market Data Parser class
 */
class MdParser {
public:
    /**
     * @brief Structure to hold the result of parsing an mbuf.
     */
    struct ParseResult {
        bool valid;                 ///< True if parsing was successful and data is valid
        const MdBookUpdate* update; ///< Pointer to the parsed book update (zero-copy)
    };

    /**
     * @brief Parses an rte_mbuf for market data.
     * @param m The rte_mbuf to parse.
     * @return ParseResult containing validity and a pointer to the MdBookUpdate if valid.
     */
    [[nodiscard]] ParseResult parse(const rte_mbuf* m);
};

#endif // _PARSER_H_