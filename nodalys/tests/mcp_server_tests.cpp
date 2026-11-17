/**
 * @file    mcp_server_tests.cpp
 * @brief   Tests for the MCP server of the nodalys command line, driven through streams.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cli/cli.h"
#include "mcp/mcp_server.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using nlohmann::json;

namespace {

/// A low-pass filter with its corner at 1 / (2 pi R C) = 159.15 Hz.
const std::string RC_LOW_PASS = "RC low-pass\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1u\n.ac dec 10 1 100k\n.end\n";

/// A divider whose output is half the supply.
const std::string DIVIDER = "Divider\nV1 in 0 DC 10\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n";

/**
 * @brief   Runs the server on the given lines, as a client would, without a process.
 * @param[in] lines  Messages the client sends, one per line.
 * @return  The lines the server answered, each parsed.
 */
std::vector<json> Exchange(const std::vector<std::string> &lines) {
    std::string text;
    for (const std::string &line : lines) {
        text += line + "\n";
    }
    std::istringstream input(text);
    std::ostringstream output;
    std::ostringstream errors;
    REQUIRE(Nodalys::RunMcpServer(input, output, errors) == EXIT_SUCCESS);

    std::vector<json> answers;
    std::istringstream answer_lines(output.str());
    std::string answer;
    while (std::getline(answer_lines, answer)) {
        const json parsed = json::parse(answer, nullptr, false);
        // Anything on the output that is not a JSON message would break the client
        REQUIRE_FALSE(parsed.is_discarded());
        answers.push_back(parsed);
    }
    return answers;
}

/// Writes a tools/call request.
std::string MakeCall(const std::string &tool, const json &arguments, const int id = 1) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"method", "tools/call"},
                {"params", {{"name", tool}, {"arguments", arguments}}}}
        .dump();
}

/**
 * @brief   Calls a tool and returns its result object.
 * @param[in] tool       Tool name.
 * @param[in] arguments  Arguments object.
 * @return  The `result` of the response, with `isError` and the text content.
 */
json CallTool(const std::string &tool, const json &arguments) {
    const std::vector<json> answers = Exchange({MakeCall(tool, arguments)});
    REQUIRE(answers.size() == 1);
    REQUIRE(answers[0].contains("result"));
    return answers[0]["result"];
}

/// The text of the one content block of a tool result.
std::string TextOf(const json &result) {
    REQUIRE(result["content"].size() == 1);
    REQUIRE(result["content"][0]["type"] == "text");
    return result["content"][0]["text"].get<std::string>();
}

/// The text of a successful tool result, parsed.
json AnswerOf(const json &result) {
    REQUIRE(result["isError"] == false);
    const json answer = json::parse(TextOf(result), nullptr, false);
    REQUIRE_FALSE(answer.is_discarded());
    return answer;
}

} // namespace

TEST_CASE("The initialize handshake answers the client's version, the tools capability and the server info", "[mcp]") {
    const std::vector<json> answers = Exchange(
        {R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})",
         R"({"jsonrpc":"2.0","method":"notifications/initialized"})"});

    // The notification gets no answer
    REQUIRE(answers.size() == 1);
    const json &result = answers[0]["result"];
    CHECK(answers[0]["jsonrpc"] == "2.0");
    CHECK(answers[0]["id"] == 1);
    CHECK(result["protocolVersion"] == "2025-03-26");
    CHECK(result["capabilities"].contains("tools"));
    CHECK(result["serverInfo"]["name"] == "nodalys");
    CHECK_FALSE(result["serverInfo"]["version"].get<std::string>().empty());
}

TEST_CASE("A client's unknown protocol version is answered with the newest one the server speaks", "[mcp]") {
    const std::vector<json> answers =
        Exchange({R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}})"});

    REQUIRE(answers.size() == 1);
    CHECK(answers[0]["id"] == "a");
    CHECK(answers[0]["result"]["protocolVersion"] == "2025-11-25");
}

TEST_CASE("The server lists its three tools, each with a description and an object schema", "[mcp]") {
    const std::vector<json> answers = Exchange({R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})"});

    REQUIRE(answers.size() == 1);
    const json &tools = answers[0]["result"]["tools"];
    REQUIRE(tools.size() == 3);
    std::vector<std::string> names;
    for (const json &tool : tools) {
        names.push_back(tool["name"].get<std::string>());
        CHECK_FALSE(tool["description"].get<std::string>().empty());
        CHECK(tool["inputSchema"]["type"] == "object");
    }
    CHECK(names == std::vector<std::string>{"check_netlist", "simulate", "list_models"});
    CHECK(tools[1]["inputSchema"]["required"] == json::array({"netlist"}));
    CHECK(tools[1]["inputSchema"]["properties"].contains("analysis"));
}

TEST_CASE("A ping gets an empty result, and blank lines and CRLF line ends are tolerated", "[mcp]") {
    const std::vector<json> answers = Exchange({"", "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\r", "   "});

    REQUIRE(answers.size() == 1);
    CHECK(answers[0]["id"] == 3);
    CHECK(answers[0]["result"] == json::object());
}

TEST_CASE("Protocol failures come back as JSON-RPC errors with their codes", "[mcp]") {
    const std::vector<json> answers = Exchange({
        "this is not json",
        R"({"jsonrpc":"2.0","id":5,"method":"resources/list"})",
        R"([{"jsonrpc":"2.0","id":6,"method":"ping"}])",
        R"({"jsonrpc":"2.0","id":8})",
        R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"no_such_tool"}})",
        R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"simulate","arguments":[1]}})",
        R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{}})",
    });

    REQUIRE(answers.size() == 7);
    CHECK(answers[0]["error"]["code"] == -32700);
    CHECK(answers[0]["id"].is_null());
    CHECK(answers[1]["error"]["code"] == -32601);
    CHECK(answers[1]["id"] == 5);
    CHECK(answers[2]["error"]["code"] == -32600);
    CHECK(answers[3]["error"]["code"] == -32600);
    CHECK(answers[4]["error"]["code"] == -32602);
    CHECK(answers[4]["error"]["message"].get<std::string>().contains("no_such_tool"));
    CHECK(answers[5]["error"]["code"] == -32602);
    CHECK(answers[6]["error"]["code"] == -32602);
}

TEST_CASE("The nodalys command line runs the server with its 'mcp' subcommand", "[mcp]") {
    std::istringstream input(R"({"jsonrpc":"2.0","id":1,"method":"ping"})"
                             "\n");
    std::ostringstream output;
    std::ostringstream errors;
    const char *const arguments[] = {"nodalys", "mcp"};

    const int code = Nodalys::RunCli(2, arguments, input, output, errors);

    CHECK(code == EXIT_SUCCESS);
    CHECK(output.str() == "{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":{}}\n");
}

TEST_CASE("check_netlist summarizes a valid netlist", "[mcp]") {
    const json answer = AnswerOf(CallTool("check_netlist", {{"netlist", RC_LOW_PASS}}));

    CHECK(answer["valid"] == true);
    CHECK(answer["title"] == "RC low-pass");
    CHECK(answer["nodes"] == json::array({"in", "out"}));
    CHECK(answer["elements"] == json{{"C", 1}, {"R", 1}, {"V", 1}});
    CHECK(answer["analyses"] == json::array({"ac"}));
}

TEST_CASE("check_netlist counts the elements of the circuit and not those inside a subcircuit", "[mcp]") {
    const std::string netlist = "Subcircuit\n"
                                ".subckt half a b\nR1 a b 1k\nR2 b 0 1k\n.ends\n"
                                "V1 in 0 5\nX1 in out half\nR3 out 0 1k\n.end\n";

    const json answer = AnswerOf(CallTool("check_netlist", {{"netlist", netlist}}));

    CHECK(answer["elements"] == json{{"R", 1}, {"V", 1}, {"X", 1}});
    CHECK(answer["subcircuits"] == json::array({"half"}));
}

TEST_CASE("check_netlist reports a bad netlist as an error result, not a protocol error", "[mcp]") {
    const json result = CallTool("check_netlist", {{"netlist", "Bad\nR1 a b\n.end\n"}});

    CHECK(result["isError"] == true);
    CHECK(TextOf(result).contains("Malformed resistor"));
}

TEST_CASE("check_netlist and simulate ask for the netlist when it is missing or of the wrong type", "[mcp]") {
    const json missing = CallTool("check_netlist", json::object());
    CHECK(missing["isError"] == true);
    CHECK(TextOf(missing).contains("netlist"));

    const json wrong_type = CallTool("simulate", {{"netlist", 42}});
    CHECK(wrong_type["isError"] == true);
    CHECK(TextOf(wrong_type).contains("must be a string"));
}

TEST_CASE("simulate answers the operating point of the netlist's own .op", "[mcp]") {
    const json answer = AnswerOf(CallTool("simulate", {{"netlist", DIVIDER}}));

    CHECK(answer["analysis"] == "op");
    CHECK_THAT(answer["node_voltages_v"]["out"].get<double>(), WithinAbs(5.0, 1e-6));
    CHECK_THAT(answer["node_voltages_v"]["in"].get<double>(), WithinAbs(10.0, 1e-6));
}

TEST_CASE("simulate runs an operating point of a netlist that has no directive, and reads node names in any case",
          "[mcp]") {
    const json answer = AnswerOf(CallTool(
        "simulate", {{"netlist", "Diode\nV1 in 0 DC 5\nR1 in a 1k\nD1 a 0 DMOD\n.model DMOD D (IS=1e-14)\n.end\n"},
                     {"nodes", json::array({"A", "V(in)"})}}));

    // A silicon diode in series with 1 k from 5 V sits near 0.7 V
    const double diode_volts = answer["node_voltages_v"]["a"].get<double>();
    CHECK(diode_volts > 0.6);
    CHECK(diode_volts < 0.8);
    CHECK_THAT(answer["node_voltages_v"]["in"].get<double>(), WithinAbs(5.0, 1e-6));
}

TEST_CASE("simulate gives the cutoff of an AC sweep from the netlist's own directive", "[mcp]") {
    const json answer = AnswerOf(CallTool("simulate", {{"netlist", RC_LOW_PASS}, {"nodes", json::array({"out"})}}));

    CHECK(answer["analysis"] == "ac");
    const json &out = answer["nodes"]["out"];
    CHECK_THAT(out["upper_cutoff_3db_hz"].get<double>(), WithinRel(159.15, 0.02));
    CHECK(out["lower_cutoff_3db_hz"].is_null());
    CHECK_THAT(out["gain_at_start_db"].get<double>(), WithinAbs(0.0, 0.01));
    CHECK_THAT(out["phase_at_stop_deg"].get<double>(), WithinAbs(-89.9, 0.2));
    CHECK(answer["frequencies"]["points"] == 51);
}

TEST_CASE("simulate takes the sweep from its arguments when the netlist has no directive", "[mcp]") {
    const std::string netlist = "RC\nV1 in 0 AC 1\nR1 in out 1k\nC1 out 0 1u\n.end\n";

    const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist},
                                                       {"analysis", "ac"},
                                                       {"start_frequency", "10"},
                                                       {"stop_frequency", "1k"},
                                                       {"points", 5},
                                                       {"nodes", json::array({"out"})}}));

    CHECK(answer["frequencies"]["start_hz"] == 10.0);
    CHECK(answer["frequencies"]["stop_hz"] == 1000.0);
    CHECK(answer["frequencies"]["points"] == 11);
}

TEST_CASE("simulate gives the extremes and final value of a transient", "[mcp]") {
    // A 5 V step into an RC of 1 ms charges to 5 V within the 10 ms of the run
    const std::string netlist =
        "Step\nV1 in 0 PULSE(0 5 0 1n 1n 1 2)\nR1 in out 1k\nC1 out 0 1u\n.tran 10u 10m\n.end\n";

    const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist}, {"nodes", json::array({"out"})}}));

    CHECK(answer["analysis"] == "tran");
    CHECK(answer["is_complete"] == true);
    const json &out = answer["nodes"]["out"];
    CHECK_THAT(out["min_v"].get<double>(), WithinAbs(0.0, 1e-3));
    CHECK_THAT(out["final_v"].get<double>(), WithinAbs(5.0, 0.01));
    CHECK_THAT(out["max_v"].get<double>(), WithinAbs(5.0, 0.01));
    // The mean of 5 (1 - e^(-t/tau)) over 10 tau is 5 (1 - 0.1) = 4.5 V
    CHECK_THAT(out["mean_v"].get<double>(), WithinAbs(4.5, 0.05));
}

TEST_CASE("simulate asks for the settings a transient lacks", "[mcp]") {
    const std::string netlist = "RC\nV1 in 0 PULSE(0 5 0 1n 1n 1 2)\nR1 in out 1k\nC1 out 0 1u\n.end\n";

    const json result = CallTool("simulate", {{"netlist", netlist}, {"analysis", "tran"}});

    CHECK(result["isError"] == true);
    CHECK(TextOf(result).contains("stop_time"));
}

TEST_CASE("simulate runs the other analyses from their arguments", "[mcp]") {
    const std::string netlist = "Divider\nV1 in 0 DC 10 AC 1\nR1 in out 1k\nR2 out 0 3k\nC1 out 0 1n\n.end\n";

    SECTION("transfer function") {
        const json answer = AnswerOf(CallTool(
            "simulate", {{"netlist", netlist}, {"analysis", "tf"}, {"output_node", "out"}, {"input_source", "V1"}}));
        CHECK_THAT(answer["gain"].get<double>(), WithinRel(0.75, 1e-6));
        CHECK_THAT(answer["input_impedance_ohm"].get<double>(), WithinRel(4000.0, 1e-6));
        CHECK_THAT(answer["output_impedance_ohm"].get<double>(), WithinRel(750.0, 1e-6));
    }
    SECTION("DC sweep") {
        const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist},
                                                           {"analysis", "dc"},
                                                           {"source", "V1"},
                                                           {"start", 0},
                                                           {"stop", "8"},
                                                           {"step", 1},
                                                           {"nodes", json::array({"out"})}}));
        CHECK(answer["points"] == 9);
        CHECK_THAT(answer["nodes"]["out"]["max_v"].get<double>(), WithinRel(6.0, 1e-6));
        CHECK_THAT(answer["nodes"]["out"]["at_sweep_value_of_max"].get<double>(), WithinAbs(8.0, 1e-9));
    }
    SECTION("pole-zero") {
        const json answer = AnswerOf(CallTool(
            "simulate", {{"netlist", netlist}, {"analysis", "pz"}, {"output_node", "out"}, {"input_source", "V1"}}));
        CHECK(answer["pole_count"] == 1);
        // The pole is at -(1/R1 + 1/R2) / C = -1.333e6 rad/s
        CHECK_THAT(answer["poles"][0]["real"].get<double>(), WithinRel(-1.3333e6, 1e-3));
    }
    SECTION("DC sensitivity") {
        const json answer =
            AnswerOf(CallTool("simulate", {{"netlist", netlist}, {"analysis", "sens"}, {"output_node", "out"}}));
        CHECK(answer["analysis"] == "sens");
        CHECK(answer["largest_sensitivities"].size() >= 2);
    }
    SECTION("noise") {
        const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist},
                                                           {"analysis", "noise"},
                                                           {"output_node", "out"},
                                                           {"input_source", "V1"},
                                                           {"start_frequency", 10},
                                                           {"stop_frequency", "1meg"}}));
        CHECK(answer["analysis"] == "noise");
        CHECK(answer["output_total"].get<double>() > 0.0);
        CHECK(answer["largest_contributors"].size() >= 1);
    }
    SECTION("AC sensitivity") {
        const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist},
                                                           {"analysis", "sens_ac"},
                                                           {"output_node", "out"},
                                                           {"input_source", "V1"},
                                                           {"start_frequency", 10},
                                                           {"stop_frequency", "1meg"}}));
        CHECK(answer["analysis"] == "sens_ac");
        CHECK(answer["element_count"].get<int>() >= 2);
    }
    SECTION("harmonic balance") {
        const std::string rectifier = "Rectifier\nV1 in 0 SIN(0 5 1k)\nD1 in out DMOD\nR1 out 0 1k\n"
                                      ".model DMOD D (IS=1e-14)\n.end\n";
        const json answer = AnswerOf(CallTool("simulate", {{"netlist", rectifier},
                                                           {"analysis", "hb"},
                                                           {"fundamental_frequency", "1k"},
                                                           {"harmonics", 6},
                                                           {"nodes", json::array({"out"})}}));
        CHECK(answer["analysis"] == "hb");
        CHECK(answer["nodes"]["out"]["dc_v"].get<double>() > 1.0);
        CHECK(answer["nodes"]["out"]["thd_percent"].get<double>() > 1.0);
    }
    SECTION("S-parameters") {
        const std::string two_port = "Pad\nP1 in 0 portnum=1 z0=50\nP2 out 0 portnum=2 z0=50\n"
                                     "R1 in out 100\n.end\n";
        const json answer = AnswerOf(CallTool("simulate", {{"netlist", two_port},
                                                           {"analysis", "sp"},
                                                           {"start_frequency", "1meg"},
                                                           {"stop_frequency", "10meg"},
                                                           {"points", 3},
                                                           {"sweep", "lin"}}));
        CHECK(answer["ports"] == 2);
        CHECK(answer["magnitudes"].contains("S21"));
    }
}

TEST_CASE("simulate reports bad arguments as error results that say how to correct the call", "[mcp]") {
    SECTION("an unknown analysis") {
        const json result = CallTool("simulate", {{"netlist", DIVIDER}, {"analysis", "fourier"}});
        CHECK(result["isError"] == true);
        CHECK(TextOf(result).contains("Unknown analysis 'fourier'"));
        CHECK(TextOf(result).contains("sens_ac"));
    }
    SECTION("a node the netlist does not have") {
        const json result = CallTool("simulate", {{"netlist", DIVIDER}, {"nodes", json::array({"nowhere"})}});
        CHECK(result["isError"] == true);
        CHECK(TextOf(result).contains("nowhere"));
    }
    SECTION("an argument of the wrong type") {
        const json result = CallTool(
            "simulate", {{"netlist", DIVIDER}, {"analysis", "tran"}, {"stop_time", "soon"}, {"time_step", 1e-6}});
        CHECK(result["isError"] == true);
        CHECK(TextOf(result).contains("'stop_time'"));
    }
    SECTION("a missing output node") {
        const json result = CallTool("simulate", {{"netlist", DIVIDER}, {"analysis", "tf"}});
        CHECK(result["isError"] == true);
        CHECK(TextOf(result).contains("output_node"));
    }
    SECTION("a bad netlist") {
        const json result = CallTool("simulate", {{"netlist", "Bad\nR1 a b\n"}});
        CHECK(result["isError"] == true);
        CHECK(TextOf(result).starts_with("Netlist error:"));
    }
    SECTION("a run that cannot be solved") {
        // Two ideal voltage sources of different values across the same nodes have no solution
        const json result = CallTool("simulate", {{"netlist", "Short\nV1 a 0 5\nV2 a 0 3\n.op\n.end\n"}});
        CHECK(result["isError"] == true);
    }
}

TEST_CASE("The compact answer of a transient of a hundred thousand points stays short", "[mcp]") {
    const std::string netlist = "Long run\nV1 in 0 SIN(0 1 1k)\nR1 in out 1k\nC1 out 0 100n\n.tran 1u 100m\n.end\n";

    const json result = CallTool("simulate", {{"netlist", netlist}, {"nodes", json::array({"in", "out"})}});

    const json answer = AnswerOf(result);
    CHECK(answer["points"].get<int>() >= 100000);
    CHECK(TextOf(result).size() < 1500);
    CHECK_THAT(answer["nodes"]["in"]["max_v"].get<double>(), WithinAbs(1.0, 0.01));
    // A sine of amplitude 1 has an RMS of 0.7071
    CHECK_THAT(answer["nodes"]["in"]["rms_v"].get<double>(), WithinAbs(0.7071, 0.01));
}

TEST_CASE("simulate gives the full data in the export format when asked, with the node names", "[mcp]") {
    const json answer = AnswerOf(CallTool("simulate", {{"netlist", DIVIDER}, {"detail", "full"}}));

    REQUIRE(answer.contains("full"));
    CHECK(answer["full"]["node_names"].contains("1"));
    CHECK(answer["full"]["data"]["analysis"] == "operating_point");
    CHECK(answer["full"]["data"].contains("node_voltages"));
}

TEST_CASE("simulate writes the full data to a file and answers only the summary", "[mcp]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nodalys_mcp_test_output.json";
    std::filesystem::remove(path);

    const json answer = AnswerOf(CallTool("simulate", {{"netlist", RC_LOW_PASS}, {"output_file", path.string()}}));

    CHECK_FALSE(answer.contains("full"));
    CHECK(answer["output_file"] == path.string());
    std::ifstream file(path);
    REQUIRE(file.good());
    const json written = json::parse(file, nullptr, false);
    REQUIRE_FALSE(written.is_discarded());
    CHECK(written["data"]["analysis"] == "ac_sweep");
    CHECK(written["data"]["frequencies"].size() == 51);
    std::filesystem::remove(path);
}

TEST_CASE("simulate leaves a full answer that is too long for a reply to a file", "[mcp]") {
    const std::string netlist = "Long\nV1 in 0 SIN(0 1 1k)\nR1 in out 1k\nC1 out 0 100n\n.tran 1u 100m\n.end\n";

    const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist}, {"detail", "full"}}));

    CHECK_FALSE(answer.contains("full"));
    CHECK(answer["full_data_omitted"].get<std::string>().contains("output_file"));
}

TEST_CASE("simulate names the other analyses a netlist asks for", "[mcp]") {
    const std::string netlist = "Two\nV1 in 0 DC 1 AC 1\nR1 in out 1k\nC1 out 0 1u\n.op\n.ac dec 5 10 1k\n.end\n";

    const json answer = AnswerOf(CallTool("simulate", {{"netlist", netlist}}));

    CHECK(answer["analysis"] == "op");
    CHECK(answer["netlist_analyses"] == json::array({"op", "ac"}));
}

TEST_CASE("list_models lists what the reader accepts, and one topic on request", "[mcp]") {
    const json everything = AnswerOf(CallTool("list_models", json::object()));
    CHECK(everything.contains("elements"));
    CHECK(everything.contains("sources"));
    CHECK(everything.contains("models"));
    CHECK(everything.contains("directives"));
    CHECK(everything.contains("expressions"));
    CHECK(everything["analyses"]["names"].size() == 11);

    const json only_models = AnswerOf(CallTool("list_models", {{"topic", "models"}}));
    CHECK(only_models.contains("models"));
    CHECK_FALSE(only_models.contains("elements"));

    const json unknown = CallTool("list_models", {{"topic", "gadgets"}});
    CHECK(unknown["isError"] == true);
}
