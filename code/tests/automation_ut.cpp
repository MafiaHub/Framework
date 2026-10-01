#include <automation/session.h>
#include <httplib.h>
#include <input/virtual_input.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
    void Require(bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
    }
    nlohmann::json Body(const httplib::Result &result, int status) {
        Require(static_cast<bool>(result), "HTTP request failed");
        Require(result->status == status, "Unexpected HTTP status");
        return nlohmann::json::parse(result->body);
    }
} // namespace

int main() {
    // Run from a checkout root. All test files stay under its builds folder.
    const auto workspace = std::filesystem::current_path();
    if (!std::filesystem::is_directory(workspace / "code/framework") || !std::filesystem::is_regular_file(workspace / "CMakeLists.txt")) {
        std::cerr << "Run FrameworkAutomationTests from the Framework checkout root\n";
        return 1;
    }
    const auto directory = workspace / "builds" / ("qa-core-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory.parent_path());
    if (!std::filesystem::create_directory(directory))
        return 1;
    try {
        Framework::Automation::Session session;
        Framework::Automation::Options options {directory, workspace, std::string(64, 'a'), "unit", true};
        session.Start(options, {{"test", true}});
        nlohmann::json endpoint;
        std::ifstream(directory / "endpoint.json") >> endpoint;
        httplib::Client client(endpoint.at("url").get<std::string>());
        client.set_default_headers({{"Authorization", "Bearer " + options.token}});
        httplib::Client unauthorized(endpoint.at("url").get<std::string>());
        Body(unauthorized.Get("/status"), 401);
        Body(client.Get("/events?after=-1"), 400);
        const nlohmann::json command = {{"request_id", "retry"}, {"operation", "input"}, {"arguments", {{"ticks", 3}}}};
        Body(client.Post("/command", command.dump(), "application/json"), 202);
        Body(client.Post("/command", command.dump(), "application/json"), 200);
        auto conflicting                  = command;
        conflicting["arguments"]["ticks"] = 4;
        Body(client.Post("/command", conflicting.dump(), "application/json"), 409);
        const auto commands = session.TakeCommands();
        Require(commands.size() == 1 && commands.front().id == "retry", "A retried command was executed twice");
        Require(session.TakeCommands().empty(), "Command queue did not drain");
        session.AdvanceTick();
        session.Complete("retry", {{"released", true}});
        Require(Body(client.Post("/command", command.dump(), "application/json"), 200).at("state") == "completed", "Retry lost its completed result");
        nlohmann::json snapshot = {{"position", {1, 2, 3}}};
        session.Publish(snapshot);
        snapshot["position"][0] = 99;
        Require(Body(client.Get("/status"), 200).at("snapshot").at("position")[0] == 1, "Published snapshot borrowed mutable state");
        const auto events = Body(client.Get("/events?after=0"), 200);
        Require(events.at("events").size() == 2 && events.at("events")[1].at("tick") == 1, "Events lost sequence or tick attribution");
        const auto cursor = events.at("last_sequence").get<std::uint64_t>();
        Require(Body(client.Get("/events?after=" + std::to_string(cursor)), 200).at("events").empty(), "Cursor repeated consumed events");
        for (int index = 0; index < 4100; ++index) session.Emit("bounded");
        const auto overflow = Body(client.Get("/events?after=0"), 200);
        Require(overflow.at("gap").get<bool>() && overflow.at("events").size() == 4096, "Lost event history was not reported");
        Body(client.Post("/command", nlohmann::json({{"request_id", "cancel-on-stop"}, {"operation", "pending"}}).dump(), "application/json"), 202);
        session.Stop();
        std::ifstream journal(directory / "events.jsonl");
        std::size_t count = 0;
        nlohmann::json lastEvent;
        for (std::string line; std::getline(journal, line);) {
            lastEvent = nlohmann::json::parse(line);
            ++count;
        }
        Require(count == 4103, "Shutdown failed to flush the event journal");
        Require(lastEvent.at("name") == "command.failed" && lastEvent.at("payload").at("request_id") == "cancel-on-stop", "Shutdown left a queued operation unfinished");
        Require(!Framework::Automation::IsInside(workspace / "../escape", workspace), "Path escaped the workspace");

        Framework::Input::VirtualInput input;
        std::array<bool, 256> keys {};
        keys['W'] = true;
        input.Apply(keys);
        Require(input.IsKeyDown('W') && input.IsKeyPressed('W'), "Virtual press was lost");
        input.Apply(keys);
        Require(input.IsKeyPressed('W'), "Repeated snapshot erased an unconsumed press");
        input.Update();
        input.Apply(keys);
        Require(input.IsKeyDown('W') && !input.IsKeyPressed('W'), "Hold generated a second press");
        input.Apply({});
        Require(input.IsKeyReleased('W') && input.IsKeyUp('W'), "Virtual release was lost");
        keys['W'] = false;
        keys['D'] = true;
        input.Apply(keys);
        Require(input.IsKeyReleased('W') && input.IsKeyPressed('D'), "A second snapshot erased another key's release");
        Require(!input.IsKeyDown(-1) && !input.IsKeyUp(256), "Invalid key was accepted");
        input.SetMousePosition(12, 34);
        int x = 0, y = 0;
        input.GetMousePosition(x, y);
        Require(x == 12 && y == 34, "Virtual cursor did not retain its coordinates");
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << " (artifacts: " << directory << ")\n";
        return 1;
    }
    std::filesystem::remove_all(directory);
    std::cout << "Framework automation protocol and virtual input checks passed\n";
    return 0;
}
