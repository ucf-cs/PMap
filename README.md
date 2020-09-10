# PMap
A persistent concurrent hash map

## Project TODO

\* means done

\> means prioritize

	Standardize performance tests.
		Tests:
			Type:
				*Node Degree: Not supported by most (ours works)
				Node connected: Supported by all
			RMAT test
			reddit real-world test
			*Random ops ("fuzzing" test)
			Create persistence tests with synthetic power failures.
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
	Fix resizing.
	Fix KCAS.
		Use it to implement hopscotch hashing.
	Run performance tests to report.
		Set up and run on Intel SDP system.
	>Research paper