vcl 4.1;

# Default backend definition. Set this to point to your content server.
backend default {
    .host = "10.10.1.2";
    .port = "80";
}

sub vcl_recv {
    #disable caching
    #return(pass);
}