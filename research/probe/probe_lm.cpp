#include "../../ngram/language_model.h"
#include "../../neural/neural_decoder.h"
#include "../../neural/sentence_model.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
// usage: probe_lm sc.lm neural.safetensors   (stdin: "ctx<TAB>sent1 sent2 ..." ; neural scoring only)
//        lines starting with '#': LM word-sequence scoring ("# w1 w2 w3")
int main(int argc, char **argv)
{
    const auto &lm = ngram::shared_language_model(std::filesystem::path(argv[1]));
    const auto *nm = neural::shared_sentence_model(argv[2]);
    std::printf("lm valid=%d neural=%p\n", lm.valid(), (const void *)nm);
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.rfind("# ", 0) == 0)
        {
            std::istringstream is(line.substr(2));
            std::string w;
            ngram::State s = lm.null_state(), o;
            double total = 0;
            std::printf("LM %s :", line.c_str() + 2);
            while (is >> w)
            {
                const auto idx = lm.index(w);
                const float sc = lm.score(s, idx, o);
                s = o;
                total += sc;
                std::printf("  [%s%s %.3f]", w.c_str(), lm.is_unknown(idx) ? "(UNK)" : "", sc);
            }
            std::printf("  => %.3f\n", total);
            continue;
        }
        const auto tab = line.find('\t');
        const std::string ctx = line.substr(0, tab);
        std::istringstream is(line.substr(tab + 1));
        std::vector<std::string> sents;
        std::string w;
        while (is >> w) sents.push_back(w);
        const auto sc = nm->score_sentences(ctx, sents);
        std::printf("NEURAL ctx=[%s]\n", ctx.c_str());
        for (size_t i = 0; i < sents.size(); ++i)
            std::printf("   %s  nats=%.3f\n", sents[i].c_str(), sc[i]);
    }
}
