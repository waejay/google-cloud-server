def isPalindrome(string):
	
	# use slice notation to find the reverse of string
	return string == string[::-1]



not_a_palindrome = "race"
is_a_palindrome = "racecar"

print(isPalindrome(not_a_palindrome))
print(isPalindrome(is_a_palindrome))


