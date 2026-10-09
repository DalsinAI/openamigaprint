/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Logins, remembered certificates and IPPS discovery: no network. */
#include "oap_net.h"
#include "oap_tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void name(OAPName *n,const char *s){CHECK(oap_dns_name_text(s,n));}
int main(void)
{
    char out[1024], md[33], text[100];
    unsigned char fp[32], back[32];
    OAPDiscovery *d = calloc(1, sizeof(*d));
    FILE *f;
    int i;
    /* MD5: RFC 1321's test suite */
    oap_md5_hex("", 0, md); CHECK(!strcmp(md, "d41d8cd98f00b204e9800998ecf8427e"));
    oap_md5_hex("abc", 3, md); CHECK(!strcmp(md, "900150983cd24fb0d6963f7d28e17f72"));
    oap_md5_hex("message digest", 14, md); CHECK(!strcmp(md, "f96b697d7cb7938d525a2f31aaf161d0"));
    oap_md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80, md);
    CHECK(!strcmp(md, "57edf4a22be3c955ac49da2e2107b67a"));
    /* Basic: RFC 7617's example */
    CHECK(oap_http_authorization("Basic realm=\"WallyWorld\"", "Aladdin", "open sesame", "POST", "/", 1, out, sizeof(out)));
    CHECK(!strcmp(out, "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\r\n"));
    /* Digest: RFC 2617 section 3.5's example (GET there) */
    oap_digest_test_cnonce = "0a4f113b";
    CHECK(oap_http_authorization("Digest realm=\"testrealm@host.com\", qop=\"auth,auth-int\", "
                                 "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"",
                                 "Mufasa", "Circle Of Life", "GET", "/dir/index.html", 1, out, sizeof(out)));
    CHECK(strstr(out, "response=\"6629fae49393a05397450978507c4ef1\"") != NULL);
    CHECK(strstr(out, "qop=auth, nc=00000001, cnonce=\"0a4f113b\"") != NULL);
    CHECK(strstr(out, "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"") != NULL);
    oap_digest_test_cnonce = NULL;
    CHECK(!oap_http_authorization("Digest realm=\"x\", nonce=\"y\", qop=\"auth-int\"", "a", "b", "POST", "/", 1, out, sizeof(out)));
    CHECK(!oap_http_authorization("Digest realm=\"x\", nonce=\"y\", algorithm=SHA-256", "a", "b", "POST", "/", 1, out, sizeof(out)));
    CHECK(!oap_http_authorization("Negotiate", "a", "b", "POST", "/", 1, out, sizeof(out)));
    /* WWW-Authenticate after a 100 Continue */
    {
        const char *r = "HTTP/1.1 100 Continue\r\nWWW-Authenticate: no\r\n\r\nHTTP/1.1 401 Unauthorized\r\n"
                        "Content-Length: 0\r\nwww-authenticate:  Digest realm=\"p\"  \r\n\r\n";
        CHECK(oap_http_header((const unsigned char *)r, strlen(r), "www-authenticate", text, sizeof(text)));
        CHECK(!strcmp(text, "Digest realm=\"p\""));
    }
    /* logins */
    f = fopen("build/test-logins.txt", "w"); CHECK(f != NULL);
    fputs("other:631\tx\ty\nprinter.local:631\tkim\tCircle Of Life\n", f); fclose(f);
    setenv("OAP_LOGIN_FILE", "build/test-logins.txt", 1);
    {
        char u[64], p[64];
        CHECK(oap_login_lookup("printer.local:631", u, sizeof(u), p, sizeof(p)) && !strcmp(u, "kim") && !strcmp(p, "Circle Of Life"));
        CHECK(!oap_login_lookup("printer.local:632", u, sizeof(u), p, sizeof(p)));
    }
    /* remembered certificates */
    for (i = 0; i < 32; i++) fp[i] = (unsigned char)(i * 7 + 1);
    oap_fingerprint_text(fp, text, sizeof(text));
    CHECK(strlen(text) == 95 && oap_fingerprint_parse(text, back) && !memcmp(fp, back, 32));
    CHECK(!oap_fingerprint_parse("AB:CD", back) && !oap_fingerprint_parse("zz", back));
    oap_trust_key("HP1234.Local", 631, out, sizeof(out)); CHECK(!strcmp(out, "hp1234.local:631"));
    f = fopen("build/test-trust.txt", "w"); CHECK(f != NULL); fclose(f);
    setenv("OAP_TRUST_FILE", "build/test-trust.txt", 1);
    CHECK(!oap_trust_lookup("hp1234.local:631", back));
    CHECK(oap_trust_remember("a.local:631", text, "A"));
    CHECK(oap_trust_remember("hp1234.local:631", text, "HP\tLaserJet"));
    CHECK(oap_trust_lookup("hp1234.local:631", back) && !memcmp(fp, back, 32));
    fp[0] ^= 0xff;
    oap_fingerprint_text(fp, text, sizeof(text));
    CHECK(oap_trust_remember("hp1234.local:631", text, "renewed"));          /* replaces, not adds */
    CHECK(oap_trust_lookup("hp1234.local:631", back) && !memcmp(fp, back, 32));
    CHECK(oap_trust_lookup("a.local:631", back));
    CHECK(!oap_trust_remember("bad\tkey", text, "x") && !oap_trust_remember("k:1", "nonsense", "x"));
    {
        char all[2048];
        size_t n;
        f = fopen("build/test-trust.txt", "r"); CHECK(f != NULL);
        n = fread(all, 1, sizeof(all) - 1, f); fclose(f); all[n] = 0;
        CHECK(strstr(all, "hp1234.local:631") == strrchr(all, '\n') - strlen(text) - strlen("renewed") - strlen("hp1234.local:631") - 2);
        CHECK(strstr(all, "HP LaserJet") == NULL);                           /* the old line went */
    }
    CHECK(oap_cert_usable(OAP_CERT_OK) && oap_cert_usable(OAP_CERT_TRUSTED) && !oap_cert_usable(OAP_CERT_ASK) &&
          !oap_cert_usable(OAP_CERT_CHANGED) && !oap_cert_usable(OAP_CERT_NONE));
    /* discovery: one printer offering both is shown once */
    d->count = 3;
    for (i = 0; i < 3; i++) d->printers[i].alive = 1;
    name(&d->printers[0].service, "HP LaserJet._ipp._tcp.local"); name(&d->printers[0].target, "hp.local");
    name(&d->printers[1].service, "HP LaserJet._ipps._tcp.local"); name(&d->printers[1].target, "hp.local");
    d->printers[1].secure = 1;
    name(&d->printers[2].service, "Epson._ipp._tcp.local"); name(&d->printers[2].target, "epson.local");
    oap_discovery_prefer(d, 1);
    CHECK(!d->printers[0].alive && d->printers[1].alive && d->printers[2].alive);
    d->printers[0].alive = 1;
    oap_discovery_prefer(d, 0);
    CHECK(d->printers[0].alive && !d->printers[1].alive && d->printers[2].alive);
    /* eligible over IPPS only with an accepted certificate */
    d->printers[1].alive = 1; strcpy(d->printers[1].uri, "ipps://hp.local:631/ipp/print"); d->printers[1].caps.pdf = OAP_PDF_YES;
    d->printers[1].cert = OAP_CERT_ASK; CHECK(!oap_pdf_eligible(&d->printers[1]));
    d->printers[1].cert = OAP_CERT_TRUSTED; CHECK(oap_pdf_eligible(&d->printers[1]));
    /* certificate state survives the discovery file */
    strcpy(d->printers[1].fingerprint, text); strcpy(d->printers[1].cert_subject, "HP1234");
    d->printers[1].cert = OAP_CERT_ASK;
    CHECK(oap_discovery_save("build/test-disc.tsv", d, 1, "done"));
    {
        OAPDiscovery *e = calloc(1, sizeof(*e));
        int done;
        char note[64];
        CHECK(oap_discovery_load("build/test-disc.tsv", e, &done, note, sizeof(note)));
        for (i = 0; i < (int)e->count; i++)
            if (e->printers[i].secure) break;
        CHECK(i < (int)e->count && e->printers[i].cert == OAP_CERT_ASK && !strcmp(e->printers[i].fingerprint, text) &&
              !strcmp(e->printers[i].cert_subject, "HP1234"));
        free(e);
    }
    free(d);
    printf("PASS: %u login, Digest, certificate memory and IPPS discovery checks\n", checks);
    return 0;
}
