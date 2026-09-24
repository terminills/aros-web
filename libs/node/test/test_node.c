#include <proto/node.h>

#include <stdio.h>

static char node_name[] = "NodeLibraryTest";
static char eval_flag[] = "-e";
static char smoke_script[] =
    "const net=require('net');"
    "const fs=require('fs');"
    "console.log('NODE_LIBRARY_JS_PASS '+process.version);"
    "const server=net.createServer(socket=>socket.pipe(socket));"
    "server.listen(0,'127.0.0.1',()=>{"
    "const client=net.connect(server.address().port,'127.0.0.1',()=>"
    "client.write('node-library'));"
    "client.once('data',data=>{"
    "if(data.toString()!=='node-library')"
    "throw new Error('loopback payload mismatch');"
    "console.log('NODE_LIBRARY_NET_PASS');"
    "fs.writeFileSync('SYS:NodeLibrary-js-net-pass.txt',"
    "'NODE_LIBRARY_JS_NET_PASS '+process.version+'\\n');"
    "client.end();"
    "server.close();"
    "});"
    "});";

int main(void)
{
    STRPTR node_argv[] = {
        (STRPTR)node_name,
        (STRPTR)eval_flag,
        (STRPTR)smoke_script,
        NULL
    };
    LONG rc;

    printf("NODE_LIBRARY_OPEN_PASS %s\n", NodeVersion());
    rc = NodeStart(3, node_argv);
    printf("NODE_LIBRARY_RETURN rc=%ld\n", (long)rc);

    return (int)rc;
}
