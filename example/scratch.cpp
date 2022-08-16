#include <curl/curl.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

static const char *urls[] = {
    "https://www.microsoft.com",
    "https://opensource.org",
    "https://www.google.com",
    "https://www.yahoo.com",
    "https://www.ibm.com",
    "https://www.mysql.com",
    "https://www.oracle.com",
    "https://www.ripe.net",
    "https://www.iana.org",
    "https://www.amazon.com",
    "https://www.netcraft.com",
    "https://www.heise.de",
    "https://www.chip.de",
    "https://www.ca.com",
    "https://www.cnet.com",
    "https://www.mozilla.org",
    "https://www.cnn.com",
    "https://www.wikipedia.org",
    "https://www.dell.com",
    "https://www.hp.com",
    "https://www.cert.org",
    "https://www.mit.edu",
    "https://www.nist.gov",
    "https://www.ebay.com",
    "https://www.playstation.com",
    "https://www.uefa.com",
    "https://www.ieee.org",
    "https://www.apple.com",
    "https://www.symantec.com",
    "https://www.zdnet.com",
    "https://www.fujitsu.com/global/",
    "https://www.supermicro.com",
    "https://www.hotmail.com",
    "https://www.ietf.org",
    "https://www.bbc.co.uk",
    "https://news.google.com",
    "https://www.foxnews.com",
    "https://www.msn.com",
    "https://www.wired.com",
    "https://www.sky.com",
    "https://www.usatoday.com",
    "https://www.cbs.com",
    "https://www.nbc.com/",
    "https://slashdot.org",
    "https://www.informationweek.com",
    "https://apache.org",
    "https://www.un.org",
};

#define MAX_PARALLEL 10 /* number of simultaneous transfers */
#define NUM_URLS sizeof(urls) / sizeof(char *)

static size_t write_cb(char *data, size_t n, size_t l, void *userp) {
  /* take care of the data here, ignored in this example */
  (void)data;
  (void)userp;
  return n * l;
}

static void add_transfer(CURLM *cm, int i) {
  CURL *eh = curl_easy_init();
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(eh, CURLOPT_URL, urls[i]);
  curl_easy_setopt(eh, CURLOPT_PRIVATE, urls[i]);
  curl_multi_add_handle(cm, eh);
}

int main(void) {
  CURLM *cm;
  CURLMsg *msg;
  unsigned int transfers = 0;
  int msgs_left = -1;
  int still_alive = 1;

  curl_global_init(CURL_GLOBAL_ALL);
  cm = curl_multi_init();

  /* Limit the amount of simultaneous connections curl should allow: */
  curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, (long)MAX_PARALLEL);

  // TODO
  // for (transfers = 0; transfers < MAX_PARALLEL; transfers++)
  //   add_transfer(cm, transfers);

  do {
    std::cout << "<< 1 >>" << std::endl;
    curl_multi_perform(cm, &still_alive);
    std::cout << "<< 2 >>" << std::endl;

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      std::cout << "<< 3 >>" << std::endl;
      if (msg->msg == CURLMSG_DONE) {
        std::cout << "<< 4 >>" << std::endl;
        char *url;
        CURL *e = msg->easy_handle;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
        std::fprintf(stderr, "R: %d - %s <%s>\n", msg->data.result,
                     curl_easy_strerror(msg->data.result), url);
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
      } else {
        std::cout << "<< 5 >>" << std::endl;
        std::fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
      }
    }
    if (still_alive) {
      std::cout << "<< 6 >>" << std::endl;
      curl_multi_poll(cm, NULL, 0, 1000, NULL);
      std::cout << "<< 7 >>" << std::endl;
    }
    if (transfers < NUM_URLS) {
      std::cout << "<< 8 >>" << std::endl;
      add_transfer(cm, transfers++);
      std::cout << "<< 9 >>" << std::endl;
    }

    std::cout << "<< 10 >>" << std::endl;
  } while (still_alive || (transfers < NUM_URLS));

  std::cout << "<< 11 >>" << std::endl;
  curl_multi_cleanup(cm);
  std::cout << "<< 12 >>" << std::endl;
  curl_global_cleanup();
  std::cout << "<< 13 >>" << std::endl;

  return EXIT_SUCCESS;
}
