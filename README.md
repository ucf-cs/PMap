# PMap
A persistent concurrent hash map

## Project TODO

\* means done

\> means prioritize

x means low priority

	Standardize performance tests.
		Tests:
			*Node Degree
			RMAT test
			reddit real-world test
			*Random ops ("fuzzing" test)
			>Create persistence tests with synthetic power failures.
		Implementations:
			Mine:
				*w/wo resize
				linear/hopscotch
				*volatile/NVM
					>Needs recovery functions
			Alternatives:
				*OneFile
				*PMDK libpmemobj concurrent hash map
				*STL hash map (coarse-grained locks)
	xFix resizing
	*Fix KCAS
		Use it to implement hopscotch hashing.
	>Run performance tests to report.
		Set up and run on Intel SDP system.
	>Research paper