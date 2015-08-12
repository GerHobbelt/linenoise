#pragma once

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>


namespace linenoisepp
{

	class LineNoise
	{
	public:
#ifndef NO_COMPLETION
		using CompletionCallback = std::function<std::vector<std::string>(const std::string& input)>;
#endif

	public:
		LineNoise();
		~LineNoise();

		std::tuple<bool, std::string>	Prompt(const std::string& prompt);

		void							WriteLine(const std::string& line);

#ifndef NO_COMPLETION
		void							SetCompletionCallback(CompletionCallback callback);
#endif

		bool							HistoryAdd(const std::string& line);
		int								HistoryGetMaxLen() const;
		bool							HistorySetMaxLen(int length);
		bool							HistorySave(const std::string& fileName);
		bool							HistoryLoad(const std::string& fileName);
		void							HistoryClear();

		int								GetColumns() const;

	private:
		struct CheshireCat;
		std::unique_ptr<CheshireCat>	d_;
	};

}
