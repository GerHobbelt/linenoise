#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "linenoise.hpp"


using namespace std;


int
main(int, char *[])
{
	const auto HistoryFile = "history.txt";

	linenoisepp::LineNoise console;

	console.HistoryLoad(HistoryFile);

#ifndef NO_COMPLETION
    console.SetCompletionCallback([] (const string& input) {
		auto completions = vector<string>{};
		if(input[0] == 'h')
		{
			completions.push_back("hello");
			completions.push_back("hello there");
		}
		return completions;
	});
#endif

	auto mythread = thread{ [&console] {
		for(int i = 0; i < 10; i++)
		{
			this_thread::sleep_for(chrono::seconds{ 1 });

			stringstream ss;
			ss << "Thread LN: i=" << i << "\n";
			console.WriteLine(ss.str());
		}
	} };

	{
		auto success = true;
		auto line = string{};
		while((tie(success, line) = console.Prompt("hello> ")), success)
		{
			if(!line.empty() && line[0] != '/')
			{
				cout << "echo: '" << line << "'\n";
				console.HistoryAdd(line);
				console.HistorySave(HistoryFile);
			}
			else if(line.find("/historylen") == 0)
			{
				try
				{
					const int len = stoi(line.substr(12));
					console.HistorySetMaxLen(len + 1);
				}
				catch(const exception&)
				{
					cout << "error: invalid number\n";
				}
			}
			else if (line[0] == '/')
			{
				cout << "Unreconized command: " << line << "\n";
			}
		}
	}

	mythread.join();

    return 0;
}
