def isPalindrome(string):
	
	# use slice notation to find the reverse of string
	return string == string[::-1]



not_a_palindrome = "race"
is_a_palindrome = "racecar"

print(isPalindrome(not_a_palindrome))
print(isPalindrome(is_a_palindrome))

def print_to_half(string):
	print(string[:len(string) / 2])


print_to_half("the half is here and rest is here")

