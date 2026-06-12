/*
 * Retirement Counter
 * Copyright (C) 2026 Sam Greadly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

struct Quote {
  const char* text;
  const char* attribution;
};

const Quote quotes[] = {
  { "The most important thing is not to win but to take part.",                    "Olympic Creed"       },
  { "It's not about the destination, it's about the journey.",                    "Unknown"             },
  { "Champions keep playing until they get it right.",                            "Billie Jean King"    },
  { "You have to expect things of yourself before you can do them.",              "Michael Jordan"      },
  { "Do not let what you cannot do interfere with what you can do.",              "John Wooden"         },
  { "The difference between the impossible and possible lies in determination.",  "Tommy Lasorda"       },
  { "It ain't about how hard you hit. It's about how hard you can get hit.",      "Rocky Balboa"        },
  { "Gold medals aren't really made of gold. They're made of sweat and will.",   "Dan Gable"           },
  { "You were born to be a player. You were meant to be here.",                  "Herb Brooks"         },
  { "Excellence is not a singular act, but a habit.",                            "Aristotle"           },
  { "The will to win means nothing without the will to prepare.",                "Juma Ikangaa"        },
  { "Hard work beats talent when talent doesn't work hard.",                     "Tim Notke"           },
  { "Do not pray for an easy life. Pray for the strength to endure a hard one.", "Bruce Lee"           },
  { "To give anything less than your best is to sacrifice the gift.",            "Steve Prefontaine"   },
  { "Pain is temporary. Quitting lasts forever.",                                "Lance Armstrong"     },
  { "Somewhere in the world someone is training when you are not.",              "Unknown"             },
  { "Make each day your masterpiece.",                                           "John Wooden"         },
  { "It always seems impossible until it's done.",                               "Nelson Mandela"      },
  { "You miss 100% of the shots you don't take.",                                "Wayne Gretzky"       },
  { "Believe you can and you're halfway there.",                                 "Theodore Roosevelt"  },
  { "The Olympics are the greatest show on Earth.",                              "Bob Costas"          },
  { "Brisbane 2032 will be the third Olympics hosted by Australia.",             "TIL"                 },
  { "The Olympic flame has been burning continuously since the 1928 Amsterdam Games.", "TIL"           },
  { "The Olympic rings represent the five continents of the world.",             "TIL"                 },
  { "Australia hosted the Olympics in Melbourne 1956 and Sydney 2000.",         "TIL"                 },
  { "The marathon distance of 42.195km was standardised at the 1908 London Games.", "TIL"             },
  { "Olympic gold medals are mostly silver — only 1.34% is actual gold.",       "TIL"                 },
  { "The Olympic motto is Citius, Altius, Fortius: Faster, Higher, Stronger.",  "TIL"                 },
  { "Brisbane was announced as host of the 2032 Games on 21 July 2021.",        "TIL"                 },
  { "The 2032 Games will span Brisbane, Gold Coast and Sunshine Coast.",        "TIL"                 },
};

const int QUOTE_COUNT = sizeof(quotes) / sizeof(quotes[0]);