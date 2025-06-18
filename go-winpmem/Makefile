all:
	GOOS=windows GOARCH=amd64 go build -o ./winpmem.exe -ldflags="-w -s " ./cmd/

# Build with a test driver - only useful for development
test_driver:
	GOOS=windows GOARCH=amd64 go build -o ./winpmem.exe -tags " testdriver " -ldflags="-w -s " ./cmd/

assets:
	rm -f embed/winpmem*
	cp ../src/binaries/winpmem_*.sys embed
	gzip -9 embed/*
