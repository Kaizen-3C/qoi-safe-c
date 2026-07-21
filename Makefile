# qoi-safe-c -- build & proof
#
#   make check   build under ASan/UBSan, regenerate the corpus, and assert the
#                safe decoder is byte-identical to the reference on every valid
#                and hostile input (sanitizer-clean on both).
#   make fuzz    structure-aware libFuzzer soak on qoi_safe_decode (T=seconds).
#
# Our decoder (src/qoi_safe.c, test/fuzz_safe.c) is held to -Werror. Units that
# include the vendored reference header are compiled without -Werror -- it is
# not our code to gate on.

CC     ?= clang
STRICT := -std=c11 -Wall -Wextra -Werror -O1 -g
LOOSE  := -std=c11 -O1 -g -w
SAN    := -fsanitize=address,undefined -fno-sanitize-recover=all

.PHONY: check fuzz clean
check:
	@mkdir -p corpus/valid corpus/hostile
	$(CC) $(LOOSE) $(SAN) test/gen_corpus.c -o gen_corpus
	./gen_corpus corpus
	cargo build --release --manifest-path rust/Cargo.toml
	$(CC) $(LOOSE)  $(SAN) -c src/original.c      -o original.o
	$(CC) $(STRICT) $(SAN) -c src/qoi_safe.c      -o qoi_safe.o
	$(CC) $(LOOSE)  $(SAN) -c test/differential.c -o differential.o
	$(CC) $(SAN) original.o qoi_safe.o differential.o -o differential
	./differential corpus rust/target/release/qoi_rs

fuzz:
	@mkdir -p corpus/valid corpus/hostile
	$(CC) $(STRICT) -fsanitize=address,undefined,fuzzer src/qoi_safe.c test/fuzz_safe.c -o fuzz_safe
	./fuzz_safe -max_total_time=$(or $(T),120) corpus/valid corpus/hostile

clean:
	rm -f gen_corpus differential fuzz_safe *.o
	rm -rf corpus/valid corpus/hostile
	cargo clean --manifest-path rust/Cargo.toml 2>/dev/null || true
