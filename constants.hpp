#pragma once

//Numeric Limits
static constexpr int64_t MAX_ASSET_AMOUNT = 4611686018427387903;
static constexpr uint64_t MAX_ASSET_AMOUNT_64 = 4611686018427387903;

//Contract names
static constexpr eosio::name POL_CONTRACT = "pol.fusion"_n;
static constexpr eosio::name SYSTEM_CONTRACT = "eosio"_n;
static constexpr eosio::name TOKEN_CONTRACT = "token.fusion"_n;
static constexpr eosio::name WAX_CONTRACT = "eosio.token"_n;

//Symbols
static constexpr eosio::symbol LSWAX_SYMBOL = eosio::symbol("LSWAX", 8);
static constexpr eosio::symbol SWAX_SYMBOL = eosio::symbol("SWAX", 8);
static constexpr eosio::symbol WAX_SYMBOL = eosio::symbol("WAX", 8);

//Assets
static const eosio::asset ZERO_LSWAX = eosio::asset(0, LSWAX_SYMBOL);
static const eosio::asset ZERO_SWAX = eosio::asset(0, SWAX_SYMBOL);
static const eosio::asset ZERO_WAX = eosio::asset(0, WAX_SYMBOL);

//Other
static constexpr uint64_t INITIAL_EPOCH_START_TIMESTAMP = 1710460800; /* 3/15/2024 00:00:00 GMT */
static constexpr uint64_t MAXIMUM_WAX_TO_RENT = 10000000; /* 10 Million WAX */
static constexpr uint64_t MINIMUM_PRODUCERS_TO_VOTE_FOR = 16;
static constexpr uint64_t MINIMUM_WAX_TO_RENT = 10; /* 10 WAX */

//System Contract
static constexpr uint32_t SECONDS_PER_DAY = 24 * 3600;
static constexpr uint32_t REFUND_DELAY_SEC = 3 * SECONDS_PER_DAY;


//Errors
static const char* ERR_CONFIG_NOT_FOUND = "could not locate config";

