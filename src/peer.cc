#include <zmqpp/zmqpp.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>
#include <thread>
#include <set>
#include <SFML/Audio.hpp>
#include "utils.cc"

using namespace std;
using namespace zmqpp;

#define PIECES_PATH  "./pieces/list"


string address, port, tracker_ip, tracker_port;


void add_remove_pieces(socket &tracker, bool is_add) {
  set<string> pieces;
  ifstream i_file(PIECES_PATH);
  string line;
  while (getline(i_file, line)) {
    pieces.insert(line);
  }

  remove(PIECES_PATH);

  ofstream o_file(PIECES_PATH);

  message request;
  request << ((is_add) ? ADD : REM )<< address << port << pieces.size();
  for (string piece : pieces) {
    o_file << piece << endl;
    request << piece;
  }

  tracker.send(request);

  i_file.close();
  o_file.close();
}

bool  share_file(socket &tracker, string &_filename) {
  const string filename = "files/" + _filename;
  if (!file_exists(filename))
    return false;

  string command = "./totient_generator.sh " + filename + " " + tracker_ip + " " + tracker_port;
  cerr << string_color(command, BLUE) << endl;
  system(command.c_str());

  if (!file_exists("./totient/" + _filename + ".totient"))
    return false;

  totient::entry totient_file("./totient/" + _filename + ".totient");

  message request;

  request << ADD << address << port << totient_file.pieces.size();

  for (size_t i = 0; i < totient_file.pieces.size(); ++i)
    request << totient_file.pieces[i];

  tracker.send(request);

  return true;
}

bool download_file(socket &tracker, unordered_map<string, totient::entry> &downloads, string &filename,
      socket &download_t) {
  filename = "./totient/" + filename + ".totient";
  if (!file_exists(filename))
    return false;

  if (downloads.count(filename) == 0) {
    totient::entry entry(filename);
    downloads[filename] = entry;
  }

  message request;
  request << "push";

  download_t.send(request);

  return true;
}

void play_song(const string &filename) {

}

void download_thread(void * _ctx) {
  context *ctx= (context *)_ctx;
  socket cli(*(ctx), socket_type::dealer);
  cli.connect("inproc://download");

  while (true) {
    message request;
    cli.receive(request);
    string command;
    request >> command;
    if (command == "quit")
      break;

    else {
      cerr << string_color("@@@ Received request in download thread", GREEN) << endl;
    }
  }
}

void play_thread(void *_ctx){
  sf::Music music;
  vector<string> playlist;
  bool playflag = true;

  context *ctx = (context*)_ctx;
  socket cli(*(ctx), socket_type::dealer);
  cli.connect("inproc://playlist");
  size_t pos = 0;

  poller pol;
  pol.add(cli);
  while (true) {
    if (playlist.size() > 0 and pos < playlist.size()) {
      string name = playlist[pos];
      if (music.getStatus() == 0 and music.openFromFile("files/" + name) and playflag){
        music.play();
        pos++;
      }
    }

    if (pol.poll(100)) {
      if (pol.has_input(cli)) {
        message incmsg;
        cli.receive(incmsg);
        string command;
        incmsg >> command;
        if (command == "quit")
          break;
        if (command == "add") {
          string filename;
          incmsg >> filename;
          playlist.push_back(filename);
        } else if (command == "next") {
          music.stop();
        } else if (command == "prev") {
          pos = pos - 2;
          if (pos < 0)
            pos = 0;
          music.stop();
        } else if (command == "stop" and playflag) {
          playflag = false;
          pos--;
          if (pos < 0)
            pos = 0;
          music.stop();
        } else if(command == "play" and not playflag) {
          playflag = "true";
        } else if(command == "del" and playlist.size() > 0) {
          pos--;
          if (pos < 0)
            pos = 0;
          playlist.erase(playlist.begin() + pos);
          music.stop();
        } else if(command == "pause" and playflag) {
          music.pause();
        } else if(command == "play" and playflag and playlist.size() > 0) {
          music.play();
        }
      }
    }
  }
}

int main(int argc, char **argv) {

  if (argc < 5) {
    cout << "Usage: " << argv[0] << "address port tracker_ip tracker_port" << endl;
    exit(1);
  }

  string notification;
  notification += string_color(string("Running peer at ") + argv[1] + " on port " + argv[2] + "\n");
  notification += string_color(string("Default tracker at ") + argv[3] + " on port " + argv[4] + "\n");


  address = argv[1];
  port    = argv[2];
  tracker_ip = argv[3];
  tracker_port = argv[4];
  const string tracker_endpoint = string("tcp://") + tracker_ip + ":" + tracker_port;

  context ctx;
  socket tracker(ctx, socket_type::dealer);
  tracker.connect(tracker_endpoint);

  add_remove_pieces(tracker, true);


  // Peer state
  unordered_map<string, totient::entry> downloads;
  // End peer state

  socket download_t(ctx, socket_type::dealer);
  download_t.bind("inproc://download");
  socket playlist_t(ctx, socket_type::dealer);
  playlist_t.bind("inproc://playlist");

  thread download_task(download_thread, (void *) &ctx);
  thread playlist_task(play_thread, (void *) &ctx);


  while (true) {
    string command;

    system("clear");
    cout << notification;
    notification = "";
    cout << "    Totient P2P file sharing." << endl;
    cout << string("options : \"share\", \"download\", \"play\", \"stop\", \"del\", \"pause\", \"quit\",") +
      " \"list_downloads\""<< endl;
    cin >> command;

    if ((command == "q") or (command == "quit"))
      break;

    string filename;
    if (command == "share") {
      cout << "Enter the name of the file that you want to share (must be in the files dir)" << endl;
      cin >> filename;
      if (share_file(tracker, filename))
        notification += string_color("The file was successfully shared\n", GREEN);
      else
        notification += string_color("The file does not exist\n", RED);
    } else if (command == "download") {
      cout << "Enter the name of the file that you want to download (must be in the totient dir)" << endl;
      cin >> filename;
      if (download_file(tracker, downloads, filename, download_t))
        notification += string_color("Download in process\n", GREEN);
      else
        notification += string_color("The file does not exist\n", RED);
    } else if (command == "add") {
      cout << "Enter the name of the file that you want to hear (must be in the files dir)" << endl;
      cin >> filename;
      message p_command;
      p_command << command << filename;
      playlist_t.send(p_command);
    } else if (command == "next" or command == "prev" or command == "stop" or command == "play" or command == "del"
        or command == "pause") {
      message p_command;
      p_command << command;
      playlist_t.send(p_command);
    } else if (command == "list_downloads") {
      int i = 0;
      for (const auto &name : downloads)
        notification += string_color("[" + to_string(i++) + "] - " + name.first + '\n', BLUE);
    } else {
      notification += string_color("Command not found\n", RED);
    }
  }


  message request;
  request << "quit";
  download_t.send(request);
  request << "quit";
  playlist_t.send(request);
  add_remove_pieces(tracker, false);

  download_task.join();
  playlist_task.join();

  cout << "Bye bye" << endl;

  return 0;
}
