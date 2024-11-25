all:
	GOOS=windows GOARCH=amd64 go build -o ./winpmem.exe -ldflags="-w -s " ./cmd/

assets:
	rm -f embed/winpmem*
	cp ../src/binaries/winpmem_*.sys embed
	gzip -9 embed/*
