#!/bin/sh

for BG in {40..47}; do
    printf "\033[${BG};30m %3d \033[m" $BG
done
echo
for BG in {100..107}; do
    printf "\033[${BG};30m %3d \033[m" $BG
done
echo
echo "The two line chunk below should look the same as the one above"
echo
for BG in {0..7}; do
    printf "\033[48;5;${BG};30m %3d \033[m" $BG
done
echo
for BG in {8..15}; do
    printf "\033[48;5;${BG};30m %3d \033[m" $BG
done

echo
echo
echo
for BG in {16..255}; do
    printf "\033[48;5;${BG};30m %3d \033[m" $BG
    if (( $BG % 6 == 3 )); then
        echo ;
    fi
done


echo

