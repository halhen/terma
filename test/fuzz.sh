#!/bin/bash

while [[ "1" ]]; do
    case $(( $RANDOM % 10 )) in
        1) 
            printf "\033[$(($RANDOM % 100));$(($RANDOM % 100))m"
            ;;
        2)
            fn=$(cat /dev/urandom | tr -dc 'A-Z[}]^_`a-z{|}~' | head -c1)
            printf "\033[$(($RANDOM % 100));$(($RANDOM % 100))$fn"
            ;;
        3)
            fn=$(cat /dev/urandom | tr -dc 'A-Z[}]^_`a-z{|}~' | head -c1)
            printf "\033$fn"
            ;;
        4)
            printf "\033[$(($RANDOM%100));$(($RANDOM%100))H"
            ;;
        5)
            for j in {1..100}; do
                echo #newlines
            done;;
        *)
            printf "\x$(printf %x $RANDOM)"
            ;;
    esac
done
