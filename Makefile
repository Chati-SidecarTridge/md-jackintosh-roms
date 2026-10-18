# Top-level helpers. Builds go through tools/dev/build.sh (EPIC-00 STORY-01).
.PHONY: format format-check tidy check clean

format:
	./scripts/clang-checks.sh format

format-check:
	./scripts/clang-checks.sh format-check

tidy:
	./scripts/clang-checks.sh tidy

check:
	./scripts/clang-checks.sh check

clean:
	rm -rf build/st-* build/ste-*
