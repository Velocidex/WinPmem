all:
	cp ../binaries/winpmem_*.sys embed
	GOOS=windows GOARCH=amd64 go build -o ./winpmem.exe -ldflags="-w -s " ./cmd/
