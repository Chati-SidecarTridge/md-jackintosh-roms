# Top-level helpers. Builds go through tools/dev/build.sh (EPIC-00 STORY-01).
.PHONY: format format-check tidy check clean check-release tag

# Version from file
VERSION := $(shell cat version.txt)

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

## Check that this tree can be released: CHANGELOG.md dates this version
check-release:
	@grep -qE '^## $(VERSION) \([0-9]{4}-[0-9]{2}-[0-9]{2}\)' CHANGELOG.md || { \
	  echo "ERROR: CHANGELOG.md has no dated '## $(VERSION) (YYYY-MM-DD)' section"; \
	  exit 1; }
	@echo "CHANGELOG.md dates $(VERSION)"

## Tag this version
tag: check-release
	git tag $(VERSION) && git push origin $(VERSION) && \
	echo "Tagged: $(VERSION)"
