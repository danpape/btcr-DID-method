#include <iostream>
#include <cstdlib>
#include <memory>
#include "bitcoinRPCFacade.h"
#include "encodeOpReturnData.h"
#include "satoshis.h"
#include "classifyInputString.h"
#include "txid2txref.h"
#include "t2tSupport.h"
#include <bitcoinapi/bitcoinapi.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "anyoption.h"

namespace pt = boost::property_tree;

struct CmdlineInput {
    std::string query ="";
    int txoIndex = 0;
    bool dryrun = false;
};

struct TransactionData {
    std::string inputString;
    std::string outputAddress;
    std::string privateKey;
    std::string ddoRef;
    double fee = 0.0;
    int txoIndex = 0;
};

struct UnspentData {
    std::string address;
    std::string txid;
    std::string scriptPubKeyHex;
    std::string redeemScript;
    int64_t amountSatoshis = 0;
    int utxoIndex = 0;
};


std::string find_homedir() {
    std::string ret;
    char * home = getenv("HOME");
    if(home != nullptr)
        ret.append(home);
    return ret;
}

int parseCommandLineArgs(int argc, char **argv,
                         struct RpcConfig &rpcConfig,
                         struct CmdlineInput &cmdlineInput,
                         struct TransactionData &transactionData) {

    auto opt = new AnyOption();
    opt->setFileDelimiterChar('=');

    opt->addUsage( "" );
    opt->addUsage( "Usage: createBtcrDid [options] <inputXXX> <outputAddress> <private key> <fee> <ddoRef>" );
    opt->addUsage( "" );
    opt->addUsage( " -h  --help                 Print this help " );
    opt->addUsage( " --rpchost [rpchost or IP]  RPC host (default: 127.0.0.1) " );
    opt->addUsage( " --rpcuser [user]           RPC user " );
    opt->addUsage( " --rpcpassword [pass]       RPC password " );
    opt->addUsage( " --rpcport [port]           RPC port (default: try both 8332 and 18332) " );
    opt->addUsage( " --config [config_path]     Full pathname to bitcoin.conf (default: <homedir>/.bitcoin/bitcoin.conf) " );
    opt->addUsage( " --txoIndex [index]         Index # of which TXO to use from the input transaction (default: 0) " );
    opt->addUsage( " -n --dryrun                Do everything except submit transaction to blockchain" );
    opt->addUsage( "" );
    opt->addUsage( "<inputXXX>      input: (bitcoin address, txid, txref) needs at least slightly more unspent BTCs than your offered fee" );
    opt->addUsage( "<outputAddress> output bitcoin address: will receive transaction change and be the basis for your DID" );
    opt->addUsage( "<private key>   private key in base58 (wallet import format)" );
    opt->addUsage( "<fee>           fee you are willing to pay (suggestion: >0.001 BTC)" );
    opt->addUsage( "<ddoRef>        reference to a DDO you want as part of your DID (optional)" );

    opt->setFlag("help", 'h');
    opt->setFlag("dryrun", 'n');
    opt->setOption("rpchost");
    opt->setOption("rpcuser");
    opt->setOption("rpcpassword");
    opt->setOption("rpcport");
    opt->setCommandOption("config");
    opt->setOption("txoIndex");

    // parse any command line arguments--this is a first pass, mainly to get a possible
    // "config" option that tells if the bitcoin.conf file is in a non-default location
    opt->processCommandArgs( argc, argv );

    // print usage if no options
    if( ! opt->hasOptions()) {
        opt->printUsage();
        delete opt;
        return 0;
    }

    // see if there is a bitcoin.conf file to parse. If not, continue.
    if (opt->getValue("config") != nullptr) {
        opt->processFile(opt->getValue("config"));
    }
    else {
        std::string home = find_homedir();
        if(!home.empty()) {
            std::string configPath = home + "/.bitcoin/bitcoin.conf";
            if(!opt->processFile(configPath.data())) {
                std::cerr << "Warning: Config file " << configPath
                          << " not readable. Perhaps try --config? Attempting to continue..."
                          << std::endl;
            }
        }
    }

    // parse command line arguments AGAIN--this is because command line args should override config file
    opt->processCommandArgs( argc, argv );


    // print usage if help was requested
    if (opt->getFlag("help") || opt->getFlag('h')) {
        opt->printUsage();
        delete opt;
        return 0;
    }

    // was dryrun requested?
    if (opt->getFlag("dryrun") || opt->getFlag('n')) {
        cmdlineInput.dryrun = true;
    }

    // see if there is an rpchost specified. If not, use default
    if (opt->getValue("rpchost") != nullptr) {
        rpcConfig.rpchost = opt->getValue("rpchost");
    }

    // see if there is an rpcuser specified. If not, exit
    if (opt->getValue("rpcuser") == nullptr) {
        std::cerr << "'rpcuser' not found. Check bitcoin.conf or command line usage." << std::endl;
        opt->printUsage();
        delete opt;
        return -1;
    }
    rpcConfig.rpcuser = opt->getValue("rpcuser");

    // see if there is an rpcpassword specified. If not, exit
    if (opt->getValue("rpcpassword") == nullptr) {
        std::cerr << "'rpcpassword' not found. Check bitcoin.conf or command line usage." << std::endl;
        opt->printUsage();
        delete opt;
        return -1;
    }
    rpcConfig.rpcpassword = opt->getValue("rpcpassword");

    // will try both well known ports (8332 and 18332) if one is not specified
    if (opt->getValue("rpcport") != nullptr) {
        rpcConfig.rpcport = std::atoi(opt->getValue("rpcport"));
    }

    // get txoIndex if given
    if (opt->getValue("txoIndex") != nullptr) {
        transactionData.txoIndex = std::atoi(opt->getValue("txoIndex"));
    }

    // get the positional arguments
    if(opt->getArgc() < 4) {
        std::cerr << "Error: all required arguments not found. Check command line usage." << std::endl;
        opt->printUsage();
        delete opt;
        return -1;
    }
    transactionData.inputString = opt->getArgv(0);
    cmdlineInput.query = opt->getArgv(0); // TODO do we need this in two places?
    transactionData.outputAddress = opt->getArgv(1);
    transactionData.privateKey = opt->getArgv(2);
    transactionData.fee = std::atof(opt->getArgv(3));
    if(opt->getArgv(4) != nullptr)
        transactionData.ddoRef = opt->getArgv(4);

    // TODO validate position arguments

    return 1;
}

void printAsJson(const std::string & txid) {
    pt::ptree root;

    if(!txid.empty()) {
        root.put("comment", "transaction submitted");
        root.put("txid", txid);
        root.put("txoIndex", 1); // TODO: always 1 for now
    }
    else
        root.put("error", "the network did not accept our transaction");

    pt::write_json(std::cout, root);
}


int main(int argc, char *argv[]) {

    struct RpcConfig rpcConfig;
    struct CmdlineInput cmdlineInput;
    struct TransactionData transactionData;

    int ret = parseCommandLineArgs(argc, argv, rpcConfig, cmdlineInput, transactionData);
    if (ret < 1) {
        std::exit(ret);
    }

    try {

        BitcoinRPCFacade btc(rpcConfig);

        blockchaininfo_t blockChainInfo = btc.getblockchaininfo();

        // 0. Determine InputType

        InputParam inputParam = classifyInputString(transactionData.inputString);

        // 1. Get unspent amount available from inputString

        UnspentData unspentData;

        if(inputParam == InputParam::address_param) {
            // get unspent amount and txid from inputAddress

            // TODO: disabling this for now since it isn't implemented correctly

            std::cerr << "Error: creating a DID from a BTC address is not currently supported." << std::endl;
            std::exit(-1);
        }
        else if(inputParam == InputParam::txref_param) {
            // decode txref
            t2t::Transaction transaction;
            // TODO temporary
            t2t::ConfigTemp configTemp;
            configTemp.query = cmdlineInput.query;
            configTemp.txoIndex = cmdlineInput.txoIndex;

            t2t::decodeTxref(btc, configTemp, transaction);

            // use txid from decoded txref and cmd-line txoIndex to get utxoInfo
            utxoinfo_t utxoinfo = btc.gettxout(transaction.txid, transactionData.txoIndex);

            if(utxoinfo.value == 0) {
                std::cerr << "Error: address at txid: " << transaction.txid
                          << " and txoIndex: " << transactionData.txoIndex
                          << " has no value." << std::endl;
                std::exit(-1);
            }

            assert(utxoinfo.scriptPubKey.addresses.size() == 1);
            unspentData.address = utxoinfo.scriptPubKey.addresses[0];
            btcaddressinfo_t btcaddressinfo = btc.getaddressinfo(unspentData.address);
            if(btcaddressinfo.isscript) {
                if(btcaddressinfo.script == "witness_v0_keyhash")
                    unspentData.redeemScript = btcaddressinfo.hex;
            }

            unspentData.txid = transaction.txid;
            unspentData.utxoIndex = transactionData.txoIndex;
            unspentData.amountSatoshis = btc2satoshi(utxoinfo.value);
            unspentData.scriptPubKeyHex = utxoinfo.scriptPubKey.hex;
        }
        else if(inputParam == InputParam::txrefext_param) {
            // decode txrefext
            t2t::Transaction transaction;
            // TODO temporary
            t2t::ConfigTemp configTemp;
            configTemp.query = cmdlineInput.query;
            configTemp.txoIndex = cmdlineInput.txoIndex;

            t2t::decodeTxref(btc, configTemp, transaction);

            // use txid and txoIndex from decoded txrefext to get utxoInfo
            utxoinfo_t utxoinfo = btc.gettxout(transaction.txid, transaction.txoIndex);

            if(utxoinfo.value == 0) {
                std::cerr << "Error: address at txid: " << transaction.txid
                          << " and txoIndex: " << transactionData.txoIndex
                          << " has no value." << std::endl;
                std::exit(-1);
            }

            assert(utxoinfo.scriptPubKey.addresses.size() == 1);
            unspentData.address = utxoinfo.scriptPubKey.addresses[0];
            btcaddressinfo_t btcaddressinfo = btc.getaddressinfo(unspentData.address);
            if(btcaddressinfo.isscript) {
                if(btcaddressinfo.script == "witness_v0_keyhash")
                    unspentData.redeemScript = btcaddressinfo.hex;
            }
            unspentData.txid = transaction.txid;
            unspentData.utxoIndex = transaction.txoIndex;
            unspentData.amountSatoshis = btc2satoshi(utxoinfo.value);
            unspentData.scriptPubKeyHex = utxoinfo.scriptPubKey.hex;
        }
        else if(inputParam == InputParam::txid_param) {
            // use cmd-line txid and cmd-line txoIndex to get utxoInfo
            utxoinfo_t utxoinfo = btc.gettxout(transactionData.inputString, transactionData.txoIndex);

            if(utxoinfo.value == 0) {
                std::cerr << "Error: address at txid: " << transactionData.inputString
                          << " and txoIndex: " << transactionData.txoIndex
                          << " has no value." << std::endl;
                std::exit(-1);
            }

            assert(utxoinfo.scriptPubKey.addresses.size() == 1);
            unspentData.address = utxoinfo.scriptPubKey.addresses[0];
            btcaddressinfo_t btcaddressinfo = btc.getaddressinfo(unspentData.address);
            if(btcaddressinfo.isscript) {
                if(btcaddressinfo.script == "witness_v0_keyhash")
                    unspentData.redeemScript = btcaddressinfo.hex;
            }
            unspentData.txid = transactionData.inputString;
            unspentData.utxoIndex = transactionData.txoIndex;
            unspentData.amountSatoshis = btc2satoshi(utxoinfo.value);
            unspentData.scriptPubKeyHex = utxoinfo.scriptPubKey.hex;
        }

        // 2. compute change needed to go to outputAddress

        int64_t change = unspentData.amountSatoshis - btc2satoshi(transactionData.fee);

        // 3. create DID transaction and submit to network

        // add input txid and tx index
        std::vector<txout_t> inputs;
        txout_t txout = {unspentData.txid, static_cast<unsigned int>(unspentData.utxoIndex)};
        inputs.push_back(txout);

        // add output address and the change amount
        // TODO validate output address
        std::map<std::string, std::string> amounts;

        // first output is the output address for the change
        amounts.insert(std::make_pair(transactionData.outputAddress, std::to_string(satoshi2btc(change)) ));

        // second output is the OP_RETURN
        std::string encoded_op_return = encodeOpReturnData(transactionData.ddoRef);
        amounts.insert(std::make_pair("data", encoded_op_return));

        std::string rawTransaction = btc.createrawtransaction(inputs, amounts);

        // sign with private key
        signrawtxinext_t signrawtxin;
        signrawtxin.txid = unspentData.txid;
        signrawtxin.n = static_cast<unsigned int>(unspentData.utxoIndex);
        signrawtxin.scriptPubKey = unspentData.scriptPubKeyHex;
        signrawtxin.redeemScript = unspentData.redeemScript;
        signrawtxin.amount = std::to_string(satoshi2btc(unspentData.amountSatoshis));

        std::string signedRawTransaction =
                btc.signrawtransactionwithkey(rawTransaction, {signrawtxin}, {transactionData.privateKey}, "ALL");

        if(signedRawTransaction.empty()) {
            std::cerr << "Error: transaction could not be signed. Check your private key." << std::endl;
            std::exit(-1);
        }

        if(cmdlineInput.dryrun) {
            std::cout << "Constructing and signing the transaction was successful, but not submitted to the Bitcoin network." << std::endl;
        }
        else {
            std::cout << "Constructing and signing the transaction was successful. Now submitting to the Bitcoin network." << std::endl;
            std::cout << "After a few minutes you can use txid2txref with the following data to compute your txref and DID." << std::endl;
            // broadcast to network
            std::string resultTxid = btc.sendrawtransaction(signedRawTransaction);
            printAsJson(resultTxid);
        }

        // TODO create a DID object and print out the DID string. Warn that it isn't valid until
        // the transaction has enough confirmations
    }
    catch(BitcoinException &e)
    {
        std::cerr << e.getCode() << " " << e.getMessage() << std::endl;
        std::exit(-1);
    }
    catch(std::runtime_error &e)
    {
        std::cerr << e.what() << std::endl;
        std::exit(-1);
    }

}
